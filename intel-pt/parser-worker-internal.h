#ifndef INTEL_PT__PARSER_WORKER_INTERNAL_H
#define INTEL_PT__PARSER_WORKER_INTERNAL_H

#include "intel-pt/internal-types.h"

typedef struct ipt_worker_state {
     u8* buffer;
     u64 pos_in_buffer;
} ipt_worker_state;


enum ipt_packet_type {
    TIP,
    TIP_OUT_OF_CONTEXT,
    PSB,
    PSBEND,
    UNKOWN,
};


enum tip_packet_type {
    TIP_TIP,
    TIP_PGE,
    TIP_PGD,
    TIP_FUP,
};


typedef struct ipt_tip_packet {
    enum tip_packet_type type;
} ipt_tip_packet;


typedef struct ipt_unkown_packet_data {
    u8 byte;
} ipt_unkown_packet_data;


typedef struct ipt_packet {
    enum ipt_packet_type type;
    union {
        ipt_tip_packet tip_data;
        ipt_unkown_packet_data unkown_data;
    };
} ipt_packet;

#endif 