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

static volatile int stop_thread = 0;
static volatile u64 __PT_DATA_COLLECTED;
static u64 __LAST_HEAD;
//static u64 __LAST_COLLECT;


/* Qemu Globals */
int pt_trace_version = 0;
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


void init_trace_gen(void) 
{
    if(pt_trace_version == 0) {
        return;
    }

    // Open dump file
    if(pt_trace_version == PT_TRACE_HARDWARE_V1 || pt_trace_version == PT_TRACE_HARDWARE_V2) {
        trace_dump = fopen("trace-dump.pt", "wb");
    } else {
        trace_dump = fopen("trace-dump.txt", "w");
    }
    
    if(trace_dump == NULL) {
         printf("Failed to open trace-dump file");
         exit(1);
    }


    // Perform version spesicic initilisation 
    if(pt_trace_version == PT_TRACE_HARDWARE_V1 || pt_trace_version == PT_TRACE_HARDWARE_V2) {
        init_ipt();
        init_ipt_mapping();
    }

    if(pt_trace_version == PT_TRACE_HARDWARE_V2) {
        pt_asm_log_file = fopen("asm-trace.txt", "w");

        if(pt_asm_log_file == NULL) {
            printf("Failed to open trace-dump file");
            exit(1);
        }
    }
}


void clean_trace_gen(void)
{
    if(pt_trace_version == 0) {
        return;
    }
    
    fclose(trace_dump);
    
    if(pt_trace_version == PT_TRACE_HARDWARE_V1 || pt_trace_version == PT_TRACE_HARDWARE_V2) {
        fclose(mapping_data);

        stop_thread = 1;

        pthread_join(trace_thread, NULL);
    }

    if(pt_trace_version == PT_TRACE_HARDWARE_V2) {
        fclose(pt_asm_log_file);
    }
}


inline void ctrace_basic_block(long guest_pc) 
{
    fprintf(trace_dump, "%lX\n", guest_pc);
}

inline void ctrace_record_mapping(long guest_pc, long host_pc) 
{
    fprintf(mapping_data, "%lX, %lX\n", guest_pc, host_pc);
}


/* ***** IPT ***** */
static void *trace_thread_proc(void *arg)
{
    const unsigned char *buffer = (const unsigned char *)aux_area;
    u64 size = header->aux_size;

    while(!stop_thread) {
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

        size_t SIZE = 0;
        if (wrapped_head > wrapped_tail) {
            SIZE = wrapped_head - wrapped_tail;
        } else {
            SIZE = size - wrapped_tail;
            SIZE += wrapped_head;
        }

        // fprintf(stderr, "OFFSET=%lu, WRO=%lu, SIZE=%lu\n", OFFSET, WRAPPED_OFFSET, SIZE);

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

    // 2401 to disable return compression
    pea.config = 0x2401; //0010001000000001

    // Open the event.
    perf_fd = syscall(SYS_perf_event_open, &pea, 0, -1, -1, 0);
    if (perf_fd < 0)
    {
        fclose(trace_dump);
        fprintf(stderr, "intel-pt: could not enable tracing\n");
        fprintf(stderr, "   Errno %i: %s\n",  errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // fprintf(stderr, "intel-pt: tracing active\n");

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

    // fprintf(stderr, "intel-pt: base=%p, data=%p, aux=%p\n", base_area, data_area, aux_area);

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


inline void ipt_trace_enter(void)
{
    // Todo: Signal to the asm out that a new mode packet is about to be sent out
    //       this is used to determine which asm relates to what blocks
    if(pt_trace_version == PT_TRACE_HARDWARE_V1 || pt_trace_version == PT_TRACE_HARDWARE_V2) {
        ioctl(perf_fd, PERF_EVENT_IOC_ENABLE);

        if(pt_trace_version == PT_TRACE_HARDWARE_V2) {
            fprintf(pt_asm_log_file, "IPT_START:\n");
        }
    }
}


inline void ipt_trace_exit(void)
{
    if(pt_trace_version == PT_TRACE_HARDWARE_V1 || pt_trace_version == PT_TRACE_HARDWARE_V2) {
        ioctl(perf_fd, PERF_EVENT_IOC_DISABLE);

        if(pt_trace_version == PT_TRACE_HARDWARE_V2) {
            fprintf(pt_asm_log_file, "IPT_STOP:\n");
        }
    }
}

/* ***** Asm Recording ***** */
inline void record_asm_block(TranslationBlock *tb)
{
    // printf("\n");
    // FILE* logfile = stdout;
    // int gen_code_size = tb->tc.size;
    // int code_size;
    // const tcg_target_ulong *rx_data_gen_ptr;
    // size_t chunk_start;
    // int insn = 0;

    // if (tcg_ctx->data_gen_ptr) {
    //     rx_data_gen_ptr = tcg_splitwx_to_rx(tcg_ctx->data_gen_ptr);
    //     code_size = (const void *)rx_data_gen_ptr - tb->tc.ptr;
    // } else {
    //     rx_data_gen_ptr = 0;
    //     code_size = gen_code_size;
    // }

    // /* Dump header and the first instruction */
    // fprintf(logfile, "OUT: [size=%d]\n", gen_code_size);
    // fprintf(logfile,
    //         "  -- guest addr 0x" TARGET_FMT_lx " + tb prologue\n",
    //         tcg_ctx->gen_insn_data[insn][0]);
    // chunk_start = tcg_ctx->gen_insn_end_off[insn];
    // disas(logfile, tb->tc.ptr, chunk_start);

    // /*
    //     * Dump each instruction chunk, wrapping up empty chunks into
    //     * the next instruction. The whole array is offset so the
    //     * first entry is the beginning of the 2nd instruction.
    //     */
    // while (insn < tb->icount) {
    //     size_t chunk_end = tcg_ctx->gen_insn_end_off[insn];
    //     if (chunk_end > chunk_start) {
    //         fprintf(logfile, "  -- guest addr 0x" TARGET_FMT_lx "\n",
    //                 tcg_ctx->gen_insn_data[insn][0]);
    //         disas(logfile, tb->tc.ptr + chunk_start,
    //                 chunk_end - chunk_start);
    //         chunk_start = chunk_end;
    //     }
    //     insn++;
    // }

    // if (chunk_start < code_size) {
    //     fprintf(logfile, "  -- tb slow paths + alignment\n");
    //     disas(logfile, tb->tc.ptr + chunk_start,
    //             code_size - chunk_start);
    // }
    // printf("\n");
}


/* ***** Misc ***** */
void pt_trace_opt_parse(const char *optarg)
{
    int trace_version = optarg[0] - '0';
    if(trace_version < 0 || trace_version > 3) {
        printf("Invalid pt-trace arg\n");
        exit(1);
    }

    pt_trace_version = trace_version + 1;
}