#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>

#include "qemu/osdep.h"
#include "qemu/typedefs.h"
#include "qemu/help_option.h"
#include "qemu/option.h"
#include "ctrace.h"

/* IPT Helpers */
#define NR_DATA_PAGES 256
#define NR_AUX_PAGES 1024
#define PAGE_SIZE 4096

#define mb() asm volatile("mfence" :: \
                              : "memory")
#define rmb() asm volatile("lfence" :: \
                               : "memory")
#define __READ_ONCE(x) (*(const volatile typeof(x) *)&(x))
#define READ_ONCE(x)    \
    ({                  \
        __READ_ONCE(x); \
    })
#define WRAPPED_OFFSET ((wrapped_tail + OFFSET) % size)

typedef unsigned long u64;


/* Functions */
static void init_save_dir(void);
static void init_ipt(void);
static void wait_for_pt_thread(void);

static FILE* open_file(const char *file_name, const char *mode);

static int get_intel_pt_perf_type(void);

/* File Globals */
static FILE *trace_dump = NULL;
static FILE *pt_dump = NULL;
static FILE *mapping_data = NULL;

static int perf_fd = -1;

static pthread_t trace_thread = 0;

static struct perf_event_mmap_page *header;
static void *base_area, *data_area, *aux_area;

static volatile int stop_thread = 0;
static volatile int reading_data = 0;

static char* save_dir = NULL;
static char* save_loc = NULL;

/* Qemu Globals */
int pt_trace_version = 0;
int pt_chain_count_limit = 0;
int set_pt_branchpoint_call = 0;
FILE* pt_asm_log_file = NULL;

QemuOptsList qemu_pt_trace_opts = {
    .name = "pt-trace",
    .implied_opt_name = "pt-trace",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_pt_trace_opts.head),
    .desc = {
        {/* end of list */}
    },
};

QemuOptsList qemu_pt_loc_opts = {
    .name = "pt-loc",
    .implied_opt_name = "pt-loc",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_pt_loc_opts.head),
    .desc = {
        {/* end of list */}
    },
};


void init_trace_gen(void) 
{
    if(pt_trace_version == 0) {
        return;
    }

    init_save_dir();

    FILE* info_file = open_file("trace.info", "w");
    fprintf(info_file, "version: %d\n", (pt_trace_version - 1));
    fclose(info_file);

    if(pt_trace_version == PT_TRACE_HARDWARE_V1 || 
       pt_trace_version == PT_TRACE_HARDWARE_V2 || 
       pt_trace_version == PT_TRACE_HARDWARE_V3 || 
       pt_trace_version == PT_TRACE_HARDWARE_V4) {
        // Init file to store intel pt binary data 
        // and another to store the mapping between guest 
        // and host ip 
        pt_dump = open_file("intel-pt-data.pt", "wb");
        mapping_data = open_file("mapping-data.mpt", "w");
    } else {
        // Create a file to store the trace 
        trace_dump = open_file("trace.txt", "w");
    }
    
    if(pt_trace_version == PT_TRACE_HARDWARE_V2 || 
       pt_trace_version == PT_TRACE_HARDWARE_V3) {
        // Create a file to store the asm of each 
        // basic block as it is generated 
        pt_asm_log_file = open_file("asm-trace.txt", "w");
    }

    if(pt_trace_version == PT_TRACE_HARDWARE_V1 || 
       pt_trace_version == PT_TRACE_HARDWARE_V2 || 
       pt_trace_version == PT_TRACE_HARDWARE_V3 || 
       pt_trace_version == PT_TRACE_HARDWARE_V4) {
        init_ipt();
    }

    if (pt_trace_version == PT_TRACE_HARDWARE_V4) {
        pt_chain_count_limit = 500;
    } else {
        pt_chain_count_limit = 1000;
    }
}


static void init_save_dir(void)
{
    save_dir = (char*)calloc(sizeof(char), 256);

    if (save_loc == NULL ) {
        // Arg not ser use default location 
        strcat(save_dir, "/home/rjw24/pt-trace-data/");
    } else {
        // Use spesified location
        strcat(save_dir, save_loc);
    }

    // Check if save dir already exists 
    struct stat st = {0};

    if (stat(save_dir, &st) != -1) {
        fprintf(stderr, 
            "Warning pt save dir already exists: %s\n", save_dir
        );
        return;
    }

    int res = mkdir(
        save_dir, 0700
    );

    if (res == -1) {
        perror("Failed to create save directory");
        exit(EXIT_FAILURE);
    }
}


static FILE* open_file(const char *file_name, const char *mode)
{
    size_t path_len = strlen(save_dir) + strlen(file_name) + 1;

    char *exact_file_path = calloc(
        sizeof(char), path_len
    );

    snprintf(
        exact_file_path, path_len, 
        "%s%s", save_dir, file_name
    );

    FILE* out_file = fopen(exact_file_path, mode);

    if(out_file == NULL) {
        fprintf(stderr, "Failed to open '%s'\n", exact_file_path);
        perror("  Error");
        exit(1);
    }

    free(exact_file_path);

    return out_file;
}


void clean_trace_gen(void)
{
    if(pt_trace_version == 0) {
        return;
    }

    if(trace_thread != 0) {
        stop_thread = 1;
        pthread_join(trace_thread, NULL);
        trace_thread = 0;
    }
    if(trace_dump != NULL)  { 
        fclose(trace_dump);
        trace_dump = NULL;
    }
    if(pt_dump != NULL) { 
        fclose(pt_dump);
        pt_dump = NULL;
    }
    if(pt_asm_log_file != NULL) {
        fclose(pt_asm_log_file);
        pt_asm_log_file = NULL;
    }
    if(mapping_data != NULL) {
        fclose(mapping_data);
        mapping_data = NULL;
    }
}


inline void ctrace_basic_block(long guest_pc) 
{
    fprintf(trace_dump, "%lX\n", guest_pc);
}

inline void ctrace_record_mapping(long guest_pc, long host_pc) 
{
    if (pt_trace_version == PT_TRACE_HARDWARE_V4) {
        host_pc += 9;
    }
    fprintf(mapping_data, "%lX, %lX\n", guest_pc, host_pc);
}


/* ***** IPT ***** */
static void wait_for_pt_thread(void) 
{
    while(reading_data) {}
}

static void *trace_thread_proc(void *arg)
{
    const unsigned char *buffer = (const unsigned char *)aux_area;
    u64 size = header->aux_size;
    u64 last_head = 0;

    while(true) {
        u64 head = READ_ONCE(header->aux_head);
        rmb();

        if (head == last_head) {
            if(stop_thread) break;
            else continue;
        }

        reading_data = 1;
        // fprintf(stderr, "STARTING To Read\n");

        u64 wrapped_head = head % size;
        u64 wrapped_tail = last_head % size;

        if(wrapped_head > wrapped_tail) {
            // Check if diff small enough to continue 
            // if ((wrapped_head - wrapped_tail) > 1024 * 20) reading_data = 1;

            // from tail --> head 
            fwrite(
                buffer + wrapped_tail, 
                wrapped_head - wrapped_tail, 
                1, pt_dump
            );
        } else {
            // Check if diff small enough to continue
            // if (((size - wrapped_tail) + wrapped_head) > 1024 * 20) reading_data = 1;

            // from tail -> size 
            fwrite(
                buffer + wrapped_tail, 
                size - wrapped_tail, 
                1, pt_dump
            );

            // from start --> head 
            fwrite(
                buffer, wrapped_head, 
                1, pt_dump
            );
        }

        last_head = head;

        u64 old_tail;

        // fprintf(
        //     stderr, "WRT=%lu WRH=%lu, H=%lu D=%lu\n", 
        //     wrapped_tail, wrapped_head, head, wrapped_head > wrapped_tail ? 
        //     wrapped_head - wrapped_tail : (size - wrapped_tail) + wrapped_head
        // );

        mb();

        do {
		    old_tail = __sync_val_compare_and_swap(&header->aux_tail, 0, 0);
	    } while (!__sync_bool_compare_and_swap(&header->aux_tail, old_tail, head));

        reading_data = 0;
    }

    return NULL;
}


static void init_ipt(void) 
{
    // Set-up the perf_event_attr structure
    struct perf_event_attr pea;
    memset(&pea, 0, sizeof(pea));
    pea.size = sizeof(pea);

    // perf event type
    pea.type = get_intel_pt_perf_type();

    // Event should start disabled, and not operate in kernel-mode.
    pea.disabled = 1;
    pea.exclude_kernel = 1;
    pea.exclude_hv = 1;
    pea.precise_ip = 2;

    // 2401 to disable return compression
    pea.config = 0x2001; //0010000000000001

    // Open the event.
    perf_fd = syscall(SYS_perf_event_open, &pea, 0, -1, -1, 0);
    if (perf_fd < 0) {
        fclose(pt_dump);
        fprintf(stderr, "intel-pt: could not enable tracing\n");
        fprintf(stderr, "   Errno %i: %s\n",  errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Map perf structures into memory
    base_area = mmap(
        NULL, (NR_DATA_PAGES + 1) * PAGE_SIZE, 
        PROT_READ | PROT_WRITE, MAP_SHARED, 
        perf_fd, 0
    );

    if (base_area == MAP_FAILED) {
        close(perf_fd);
        fclose(pt_dump);

        fprintf(stderr, "intel-pt: could not map data area\n");
        fprintf(stderr, "   Errno %i: %s\n",  errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    header = base_area;
    data_area = base_area + header->data_offset;

    header->aux_offset = header->data_offset + header->data_size;
    header->aux_size = NR_AUX_PAGES * PAGE_SIZE;

    aux_area = mmap(
        NULL, header->aux_size, 
        PROT_READ | PROT_WRITE, MAP_SHARED, 
        perf_fd, header->aux_offset
    );

    if (aux_area == MAP_FAILED) {
        munmap(base_area, (NR_DATA_PAGES + 1) * PAGE_SIZE);
        close(perf_fd);
        fclose(pt_dump);

        fprintf(stderr, "intel-pt: could not map aux area\n");
        fprintf(stderr, "   Errno %i: %s\n",  errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    pthread_create(&trace_thread, NULL, trace_thread_proc, NULL);

    cpu_set_t cpuset; 
    CPU_ZERO(&cpuset);
    for(int i = 3; i < 6; i++)
        CPU_SET(i, &cpuset);

    if (pthread_setaffinity_np(trace_thread, sizeof(cpuset), &cpuset) != 0) {
        printf("Failed to set trace thread affinity\n");
        exit(EXIT_FAILURE);
    }


    pthread_t curr = pthread_self();

    cpu_set_t cpuset_; 
    CPU_ZERO(&cpuset_);
    for(int i = 0; i < 3; i++)
        CPU_SET(i, &cpuset_);

    if(pthread_setaffinity_np(curr, sizeof(cpuset_), &cpuset_) != 0) {
        printf("Failed to set qemu thread affinity\n");
        exit(EXIT_FAILURE);
    }
}


static int get_intel_pt_perf_type(void)
{
    // The Intel PT type is dynamic, so read it from the relevant file.
    int intel_pt_type_fd = open("/sys/bus/event_source/devices/intel_pt/type", O_RDONLY);
    if (intel_pt_type_fd < 0)
    {
        fprintf(stderr, "intel-pt: could not find type descriptor - is intel pt available?\n");
        exit(EXIT_FAILURE);
    }

    char type_number[16] = {0};
    int bytes_read = read(intel_pt_type_fd, type_number, sizeof(type_number) - 1);
    close(intel_pt_type_fd);

    if (bytes_read == 0)
    {
        fprintf(stderr, "intel-pt: type descriptor read error\n");
        exit(EXIT_FAILURE);
    }

    return atoi(type_number);
}



inline void ipt_helper_enter(void) 
{
    ioctl(perf_fd, PERF_EVENT_IOC_ENABLE);
}


inline void ipt_helper_exit(void) 
{
    ioctl(perf_fd, PERF_EVENT_IOC_DISABLE);
}


inline void ipt_trace_enter(void)
{
    if(pt_trace_version == PT_TRACE_HARDWARE_V1 || 
       pt_trace_version == PT_TRACE_HARDWARE_V2 || 
       pt_trace_version == PT_TRACE_HARDWARE_V3 || 
       pt_trace_version == PT_TRACE_HARDWARE_V4) {
        wait_for_pt_thread();
        ioctl(perf_fd, PERF_EVENT_IOC_ENABLE);

        if(pt_trace_version == PT_TRACE_HARDWARE_V2|| 
           pt_trace_version == PT_TRACE_HARDWARE_V3) {
            fprintf(pt_asm_log_file, "IPT_START:\n");
        }
    }
}


inline void ipt_trace_exit(void)
{
    if(pt_trace_version == PT_TRACE_HARDWARE_V1 || 
       pt_trace_version == PT_TRACE_HARDWARE_V2 || 
       pt_trace_version == PT_TRACE_HARDWARE_V3 || 
       pt_trace_version == PT_TRACE_HARDWARE_V4) {
        ioctl(perf_fd, PERF_EVENT_IOC_DISABLE);

        if(pt_trace_version == PT_TRACE_HARDWARE_V2 ||
           pt_trace_version == PT_TRACE_HARDWARE_V3) {
            fprintf(pt_asm_log_file, "IPT_STOP:\n");
        }
    }
}


inline void ipt_trace_exception_enter(void)
{
    if(pt_trace_version == PT_TRACE_HARDWARE_V1 || 
       pt_trace_version == PT_TRACE_HARDWARE_V2 || 
       pt_trace_version == PT_TRACE_HARDWARE_V3 || 
       pt_trace_version == PT_TRACE_HARDWARE_V4) {
        ioctl(perf_fd, PERF_EVENT_IOC_ENABLE);

        if(pt_trace_version == PT_TRACE_HARDWARE_V2 ||
           pt_trace_version == PT_TRACE_HARDWARE_V3) {
            fprintf(pt_asm_log_file, "IPT_START: Exception\n");
        }
    }
}


inline void ipt_trace_exception_exit(void)
{
    if(pt_trace_version == PT_TRACE_HARDWARE_V1 || 
       pt_trace_version == PT_TRACE_HARDWARE_V2 || 
       pt_trace_version == PT_TRACE_HARDWARE_V3 || 
       pt_trace_version == PT_TRACE_HARDWARE_V4) {
        ioctl(perf_fd, PERF_EVENT_IOC_DISABLE);

        if(pt_trace_version == PT_TRACE_HARDWARE_V2 ||
           pt_trace_version == PT_TRACE_HARDWARE_V3) {
            fprintf(pt_asm_log_file, "IPT_STOP: Exception\n");
        }
    }
}


inline void ipt_breakpoint_call(void) { /* This function is ment to do nothing */ }


/* ***** Misc ***** */
void pt_trace_opt_parse(const char *optarg)
{
    int trace_version = optarg[0] - '0';
    if(trace_version < 0 || trace_version > 5) {
        printf("Invalid pt-trace arg\n");
        exit(1);
    }

    pt_trace_version = trace_version + 1;
}

void pt_trace_loc_opt_parse(const char *optarg) 
{
    // Todo rjw24: check if this is less than 256
    int length = strlen(optarg);

    save_loc = (char*)calloc(sizeof(char), length + 2);

    strcpy(save_loc, optarg);

    if(save_loc[length - 1] != '/') {
        save_loc[length] = '/';
        save_loc[length + 1] = 0;
    }
}