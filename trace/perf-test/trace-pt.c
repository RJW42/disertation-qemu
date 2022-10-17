/*
 * Intel PT Tracing Support
 *
 *  Copyright (c) 2020 Tom Spink
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
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

#define RATE_IN_HZ 1000
#define PERIOD (1000000000ull / RATE_IN_HZ)

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

typedef unsigned long u64;

static int perf_fd = -1;
static pthread_t trace_thread, rate_thread, coll_thread;
static struct perf_event_mmap_page *header;
static void *base_area, *data_area, *aux_area;
static FILE *trace_data;

void ipt_trace_init();
void ipt_trace_cleanup(void);

static inline void ipt_trace_exit(void);
static inline void ipt_trace_enter(void);

static int get_intel_pt_perf_type(void);
static void *trace_thread_proc(void *arg);
static void *coll_thread_proc(void *arg);

volatile u64 __PT_DATA_COLLECTED;
u64 __LAST_HEAD;
u64 __LAST_COLLECT;

static u64 last_ip;

static u64 ticks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_nsec + (ts.tv_sec * 1000000000ull);
}

static char TNT_LONG_OPPCODE[2] = { 0x02,  0xA3};
#define TNT_LONG_OPPCODE_LENGTH 2
#define TNT_LONG_PACKET_LENGTH 8

static char PIP_OPPCODE[2] = { 0x02, 0x43};
#define PIP_OPPCODE_LENGTH 2
#define PIP_PACKET_LENGTH 8

static char PSB_OPPCODE[16] = {0x02, 0x82, 0x02, 0x82, 0x02, 0x82, 0x02, 0x82, 0x02, 0x82, 0x02, 0x82, 0x02, 0x82, 0x02, 0x82};
#define PSB_OPPCODE_LENGTH 16
#define PSB_PACKET_LENGTH 16

static char PSBEND_OPPCODE[2] = {0x02, 0x23};
#define PSBEND_OPPCODE_LENGTH 2
#define PSBEND_PACKET_LENGTH 2

static char PAD_OPPCODE[1] = {0x00};
#define PAD_OPPCODE_LENGTH 1
#define PAD_PACKET_LENGTH 1

static char MNT_OPPCODE[3] = {0x02, 0xC3, 0x88};
#define MNT_OPPCODE_LENGTH 3
#define MNT_PACKET_LENGTH 11

#define WRAPPED_OFFSET ((wrapped_tail + OFFSET) % size)
#define GET_BYTE(n) (buffer[WRAPPED_OFFSET + (n)])
#define CURRENT_BYTE GET_BYTE(0)
#define CURRENT_PTR &GET_BYTE(0)
#define NEXT_BYTE GET_BYTE(1)

#define ADVANCE(n)   \
    do               \
    {                \
        OFFSET += n; \
    } while (0)
#define REMAINING (SIZE - OFFSET)
#define LEFT(n) (REMAINING > (n))
#define BIT(x) (1U << (x))

int func(int i) {
    return i + 1;
}

int main() 
{
    int n = 1;

    ipt_trace_init();
    ipt_trace_enter();

    int i = func(n);

    ipt_trace_exit();

    printf("%d\n", i);

    {
        const unsigned char *buffer = (const unsigned char *)aux_area;
        u64 size = header->aux_size;
        u64 head = READ_ONCE(header->aux_head);
        rmb();

        if (head == __LAST_HEAD)
            return EXIT_FAILURE;

        u64 wrapped_head = head % size;
        u64 wrapped_tail = __LAST_HEAD % size;
        u64 orig_collected = __PT_DATA_COLLECTED;

        if (wrapped_head > wrapped_tail)
        {
            // from tail --> head
            fwrite(buffer + wrapped_tail, wrapped_head - wrapped_tail, 1, trace_data);
            __PT_DATA_COLLECTED += wrapped_head - wrapped_tail;
        } else {
            // from tail --> size
            fwrite(buffer + wrapped_tail, size - wrapped_tail, 1, trace_data);
            __PT_DATA_COLLECTED += size - wrapped_tail;

            // from 0 --> head
            fwrite(buffer, wrapped_head, 1, trace_data);
            __PT_DATA_COLLECTED += wrapped_head;
        }

        size_t SIZE = 0, OFFSET = 0;
        if (wrapped_head > wrapped_tail) {
            SIZE = wrapped_head - wrapped_tail;
        } else {
            SIZE = size - wrapped_tail;
            SIZE += wrapped_head;
        }

        fprintf(stderr, "OFFSET=%lu, WRO=%llu, SIZE=%lu\n", OFFSET, WRAPPED_OFFSET, SIZE);

        __LAST_HEAD = head;
        __PT_DATA_COLLECTED += SIZE;

        mb();
        header->aux_tail = head;
    }

    ipt_trace_cleanup();
}

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
        u64 orig_collected = __PT_DATA_COLLECTED;

        if (wrapped_head > wrapped_tail)
        {
            // from tail --> head
            fwrite(buffer + wrapped_tail, wrapped_head - wrapped_tail, 1, trace_data);
            __PT_DATA_COLLECTED += wrapped_head - wrapped_tail;
        } else {
            // from tail --> size
            fwrite(buffer + wrapped_tail, size - wrapped_tail, 1, trace_data);
            __PT_DATA_COLLECTED += size - wrapped_tail;

            // from 0 --> head
            fwrite(buffer, wrapped_head, 1, trace_data);
            __PT_DATA_COLLECTED += wrapped_head;
        }

        size_t SIZE = 0, OFFSET = 0;
        if (wrapped_head > wrapped_tail) {
            SIZE = wrapped_head - wrapped_tail;
        } else {
            SIZE = size - wrapped_tail;
            SIZE += wrapped_head;
        }

        fprintf(stderr, "OFFSET=%lu, WRO=%llu, SIZE=%lu\n", OFFSET, WRAPPED_OFFSET, SIZE);

        __LAST_HEAD = head;
        __PT_DATA_COLLECTED += SIZE;

        mb();
        header->aux_tail = head;
    }

    return NULL;
}

static void *coll_thread_proc(void *arg)
{
    u64 last = 0;
    u64 ldc = 0;

    while (1)
    {
        u64 cur = 0;
        do
        {
            cur = ticks();
        } while (cur < (last + 2000000000ull));
        last = cur;

        u64 dc = __PT_DATA_COLLECTED;

        u64 delta = dc - ldc;
        u64 rate = (delta / 2) / 1024 / 1024; // Mbytes/sec

        fprintf(stderr, "total: %llu bytes (delta %llu, rate %llu)\n", dc, delta, rate);
        ldc = __PT_DATA_COLLECTED;
    }
}

// --- Perf Interface --- //

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



void ipt_trace_init()
{
    // Open and/or create the trace data file
    trace_data = fopen("./trace.pt", "wb");
    if (!trace_data)
    {
        fprintf(stderr, "intel-pt: could not open trace data file for writing\n");
        exit(EXIT_FAILURE);
    }

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
        fclose(trace_data);
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
        fclose(trace_data);

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
        fclose(trace_data);

        fprintf(stderr, "intel-pt: could not map aux area\n");
        fprintf(stderr, "   Errno %i: %s\n",  errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "intel-pt: base=%p, data=%p, aux=%p\n", base_area, data_area, aux_area);

    //pthread_create(&trace_thread, NULL, trace_thread_proc, NULL);
    //pthread_create(&coll_thread, NULL, coll_thread_proc, NULL);
}

void ipt_trace_cleanup(void)
{
    fprintf(stderr, "pt: total: %llu bytes\n", __PT_DATA_COLLECTED);

    if (perf_fd >= 0)
    {
        close(perf_fd);
        fclose(trace_data);
    }
}

static inline void ipt_trace_enter(void)
{
    //if (perf_fd >= 0)
    //{
        ioctl(perf_fd, PERF_EVENT_IOC_ENABLE);
    //}
}

static inline void ipt_trace_exit(void)
{
    //if (perf_fd >= 0)
    //{
        ioctl(perf_fd, PERF_EVENT_IOC_DISABLE);
    //}
}
