#ifndef INTEL_PT__INTERNAL_TYPES_H
#define INTEL_PT__INTERNAL_TYPES_H

typedef unsigned long u64;
typedef unsigned char u8;

typedef struct IntelPTDataBuffer {
    u8* buffer;
    u64 size;
    u64 write_head;
    u64 parse_tail;
} IntelPTDataBuffer;

#endif