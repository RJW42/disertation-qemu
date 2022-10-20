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
#include "qemu/help_option.h"
#include "qemu/option.h"
#include "ctrace.h"

/* IPT Helpers */
#define NR_DATA_PAGES 512
#define NR_AUX_PAGES 1024

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
static void init_ipt(void);
static void init_ipt_mapping(void);

static int get_intel_pt_perf_type(void);

/* File Globals */
static FILE *trace_dump = NULL;
static FILE *mapping_data = NULL;

static int perf_fd = -1;

static pthread_t trace_thread/*, rate_thread, coll_thread*/;

static struct perf_event_mmap_page *header;
static void *base_area, *data_area, *aux_area;

static volatile u64 __PT_DATA_COLLECTED;
static u64 __LAST_HEAD;
//static u64 __LAST_COLLECT;


/* Qemu Globals */
int pt_trace_version = 0;


QemuOptsList qemu_pt_trace_opts = {
    .name = "pt-trace",
    .implied_opt_name = "pt-trace",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_pt_trace_opts.head),
    .desc = {
        {/* end of list */}
    },
};


void init_trace_gen(void) 
{
    if(pt_trace_version == 0) {
        return;
    }

    // Open dump file
    if(pt_trace_version == PT_TRACE_HARDWARE_V1) {
        trace_dump = fopen("trace-dump.pt", "wb");
    } else {
        trace_dump = fopen("trace-dump.txt", "w");
    }
    
    if(trace_dump == NULL) {
         printf("Failed to open trace-dump file");
         exit(1);
    }


    // Perform version spesicic initilisation 
    if(pt_trace_version == PT_TRACE_HARDWARE_V1) {
        init_ipt();
        init_ipt_mapping();
    }
}


void clean_trace_gen(void)
{
    if(pt_trace_version == 0) {
        return;
    }
    
    fclose(trace_dump);
    
    if(pt_trace_version == PT_TRACE_HARDWARE_V1) {
        fclose(mapping_data);
    }
}


inline void ctrace_basic_block(long guest_pc) 
{
    fprintf(trace_dump, "%lu\n", guest_pc);
}

inline void ctrace_record_mapping(long guest_pc, long host_pc) 
{
    fprintf(mapping_data, "%lu, %lu\n", guest_pc, host_pc);
}


/* ***** IPT ***** */
static void *trace_thread_proc(void *arg)
{
    const unsigned char *buffer = (const unsigned char *)aux_area;
    u64 size = header->aux_size;

    while(1) {
        u64 head = READ_ONCE(header->aux_head);
        rmb();

        if (head == __LAST_HEAD)
            continue;

        u64 wrapped_head = head % size;
        u64 wrapped_tail = __LAST_HEAD % size;

        if (wrapped_head > wrapped_tail)
        {
            // from tail --> head
            fwrite(buffer + wrapped_tail, wrapped_head - wrapped_tail, 1, trace_dump);
            __PT_DATA_COLLECTED += wrapped_head - wrapped_tail;
        } else {
            // from tail --> size
            fwrite(buffer + wrapped_tail, size - wrapped_tail, 1, trace_dump);
            __PT_DATA_COLLECTED += size - wrapped_tail;

            // from 0 --> head
            fwrite(buffer, wrapped_head, 1, trace_dump);
            __PT_DATA_COLLECTED += wrapped_head;
        }

        size_t SIZE = 0, OFFSET = 0;
        if (wrapped_head > wrapped_tail) {
            SIZE = wrapped_head - wrapped_tail;
        } else {
            SIZE = size - wrapped_tail;
            SIZE += wrapped_head;
        }

        fprintf(stderr, "OFFSET=%lu, WRO=%lu, SIZE=%lu\n", OFFSET, WRAPPED_OFFSET, SIZE);

        __LAST_HEAD = head;
        __PT_DATA_COLLECTED += SIZE;

        mb();
        header->aux_tail = head;
    }

    return NULL;
}


static void init_ipt_mapping(void) 
{
    mapping_data = fopen("mapping-data.mpt", "w");

    if(mapping_data == NULL) {
         printf("Failed to open mapping-data file");
         exit(1);
    }

    fprintf(mapping_data, "guest_pc, target_pc\n");
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
    pea.precise_ip = 3;

    pea.config = 0x2001;

    // Open the event.
    perf_fd = syscall(SYS_perf_event_open, &pea, 0, -1, -1, 0);
    if (perf_fd < 0)
    {
        fclose(trace_dump);
        fprintf(stderr, "intel-pt: could not enable tracing\n");
        fprintf(stderr, "   Errno %i: %s\n",  errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "intel-pt: tracing active\n");

    // Map perf structures into memory
    base_area = mmap(NULL, (NR_DATA_PAGES + 1) * 4096, PROT_READ | PROT_WRITE, MAP_SHARED, perf_fd, 0);
    if (base_area == MAP_FAILED)
    {
        close(perf_fd);
        fclose(trace_dump);

        fprintf(stderr, "intel-pt: could not map data area\n");
        fprintf(stderr, "   Errno %i: %s\n",  errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    header = base_area;
    data_area = base_area + header->data_offset;

    header->aux_offset = header->data_offset + header->data_size;
    header->aux_size = NR_AUX_PAGES * 4096;

    aux_area = mmap(NULL, header->aux_size, PROT_READ | PROT_WRITE, MAP_SHARED, perf_fd, header->aux_offset);
    if (aux_area == MAP_FAILED)
    {
        munmap(base_area, (NR_DATA_PAGES + 1) * 4096);
        close(perf_fd);
        fclose(trace_dump);

        fprintf(stderr, "intel-pt: could not map aux area\n");
        fprintf(stderr, "   Errno %i: %s\n",  errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "intel-pt: base=%p, data=%p, aux=%p\n", base_area, data_area, aux_area);

    pthread_create(&trace_thread, NULL, trace_thread_proc, NULL);
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


void ipt_trace_enter(void)
{
    ioctl(perf_fd, PERF_EVENT_IOC_ENABLE);
}


void ipt_trace_exit(void)
{
    ioctl(perf_fd, PERF_EVENT_IOC_DISABLE);
}


/* ***** Misc ***** */
void pt_trace_opt_parse(const char *optarg)
{
    int trace_version = optarg[0] - '0';
    if(trace_version < 0 || trace_version > 2) {
        printf("Invalid pt-trace arg\n");
        exit(1);
    }

    pt_trace_version = trace_version + 1;
}