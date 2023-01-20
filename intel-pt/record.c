#include "qemu/osdep.h"
#include "qemu/typedefs.h"
#include "intel-pt/record.h"
#include "intel-pt/record-internal.h"
#include "intel-pt/internal-types.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <stdatomic.h>
#include <errno.h>

#define mb() asm volatile("mfence" :: \
                              : "memory")
#define rmb() asm volatile("lfence" :: \
                               : "memory")
#define __READ_ONCE(x) (*(const volatile typeof(x) *)&(x))
#define READ_ONCE(x)    \
    ({                  \
        __READ_ONCE(x); \
    })

#define IPT_DATA_BUFFER_SIZE 1073741824
#define NR_DATA_PAGES 256
#define NR_AUX_PAGES 1024
#define PAGE_SIZE 4096

static bool enabled = false;
static int perf_fd = -1;

static pthread_t recording_thread = 0;

static struct perf_event_mmap_page *header;
static void *base_area, *data_area, *aux_area;

static volatile bool setup_intel_pt = false;
static volatile bool qemu_exec_finished = false;
static volatile bool reading_data = false;

static void wait_for_pt_record_thread(void);

void intel_pt_start(void) 
{
    if (!enabled) return;
    
    wait_for_pt_record_thread();

    ioctl(perf_fd, PERF_EVENT_IOC_ENABLE);
}

void intel_pt_stop(void)
{
    if (!enabled) return;

    ioctl(perf_fd, PERF_EVENT_IOC_DISABLE);
}


static void wait_for_pt_record_thread(void) 
{
    while(reading_data) {}
}

void intel_pt_recording_signal_qemu_end(void)
{
    qemu_exec_finished = true;
    
    pthread_join(recording_thread, NULL);
}


static void *intel_pt_recording_thread_proc(void *arg);

void intel_pt_recording_init(
    int version, volatile IntelPTDataBuffer* intel_pt_buffer
) {
    enabled = true;

    intel_pt_buffer->size = IPT_DATA_BUFFER_SIZE;
    intel_pt_buffer->write_head = 0;
    intel_pt_buffer->buffer = (u8*) calloc(
        IPT_DATA_BUFFER_SIZE, sizeof(u8)
    );

    if (intel_pt_buffer->buffer == NULL) {
        fprintf(stderr, 
            "Failed to calloc internal intel pt data buffer\n"
        );
        exit(EXIT_FAILURE);
    }

    pthread_create(
        &recording_thread, NULL, 
        intel_pt_recording_thread_proc, (void*) intel_pt_buffer
    );

    while(!setup_intel_pt) {}
}


static int get_intel_pt_perf_type(void);
static void make_perf_syscall(void);
static void map_base_area(void);
static void map_data_aux_areas(void);


static void *intel_pt_recording_thread_proc(void *arg)
{
    // Initise recording
    make_perf_syscall();
    map_base_area();
    map_data_aux_areas();

    setup_intel_pt = true;

    // Start recording data
    const u8 *aux_buffer = (const u8*) aux_area;

    volatile IntelPTDataBuffer* ipt_buffer = (volatile IntelPTDataBuffer*) arg;
    
    u64 size = header->aux_size;
    u64 last_head = 0;

    while(true) {
        u64 head = READ_ONCE(header->aux_head);
        rmb();

        if (head == last_head) {
            if (qemu_exec_finished) break;
            else continue;
        }

        reading_data = true;

        u64 wrapped_head = head % size;
        u64 wrapped_tail = last_head % size;
        u64 old_tail;

        // TODO: handle the buffer filling up
        if (wrapped_head > wrapped_tail) { 
            // write from tail --> head 
            const size_t amount_to_read = wrapped_head - wrapped_tail;

            memcpy(
                ipt_buffer->buffer + ipt_buffer->write_head, 
                aux_buffer + wrapped_tail, 
                amount_to_read
            );
            
            ipt_buffer->write_head += amount_to_read;
        } else {
            // Write from tail -> size
            const size_t fst_amount_to_read = size - wrapped_tail;

            memcpy(
                ipt_buffer->buffer + ipt_buffer->write_head,
                aux_buffer + wrapped_tail,
                fst_amount_to_read
            );

            ipt_buffer->write_head += fst_amount_to_read;

            // Write from start -> head
            const size_t snd_amount_to_read = wrapped_head;

            memcpy(
                ipt_buffer->buffer + ipt_buffer->write_head,
                aux_buffer, snd_amount_to_read
            );

            ipt_buffer->write_head += snd_amount_to_read;
        }

        last_head = head;

        do {
		    old_tail = __sync_val_compare_and_swap(&header->aux_tail, 0, 0);
	    } while (!__sync_bool_compare_and_swap(&header->aux_tail, old_tail, head));

        reading_data = false;
    }

    return NULL;
}


static void make_perf_syscall(void)
{
    struct perf_event_attr pea;
    
    memset(&pea, 0, sizeof(pea));
    
    pea.size = sizeof(pea);
    pea.type = get_intel_pt_perf_type();
    pea.disabled = 1;
    pea.exclude_kernel = 1;
    pea.exclude_hv = 1;
    pea.precise_ip = 2;

    pea.config = 0x2001;

    perf_fd = syscall(SYS_perf_event_open, 
        &pea, getppid(), -1, -1, 0
    );
    
    if (perf_fd < 0) {
        fprintf(stderr, "intel-pt: could not enable tracing\n");
        fprintf(stderr, "   Errno %i: %s\n",  errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
}


static void map_base_area(void)
{
    base_area = mmap(
        NULL, (NR_DATA_PAGES + 1) * PAGE_SIZE, 
        PROT_READ | PROT_WRITE, MAP_SHARED, 
        perf_fd, 0
    );

    if (base_area == MAP_FAILED) {
        close(perf_fd);
        fprintf(stderr, "intel-pt: could not map data area\n");
        fprintf(stderr, "   Errno %i: %s\n",  errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
}


static void map_data_aux_areas(void)
{
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
        fprintf(stderr, "intel-pt: could not map aux area\n");
        fprintf(stderr, "   Errno %i: %s\n",  errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
}


static int get_intel_pt_perf_type(void)
{
    int intel_pt_type_fd = open(
        "/sys/bus/event_source/devices/intel_pt/type", O_RDONLY
    );

    if (intel_pt_type_fd < 0) {
        fprintf(stderr, 
            "intel-pt: could not find type descriptor"
            " - is intel pt available?\n"
        );
        exit(EXIT_FAILURE);
    }

    char type_number[16] = {0};
    
    int bytes_read = read(
        intel_pt_type_fd, type_number, sizeof(type_number) - 1
    );

    close(intel_pt_type_fd);

    if (bytes_read == 0) {
        fprintf(stderr, 
            "intel-pt: type descriptor read error\n"
        );
        exit(EXIT_FAILURE);
    }

    return atoi(type_number);
}