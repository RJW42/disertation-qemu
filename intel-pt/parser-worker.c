#include "qemu/osdep.h"
#include "intel-pt/parser-worker.h"
#include "intel-pt/internal-types.h"
#include "intel-pt/intel-pt-oppcodes.h"
#include "intel-pt/parser-worker-internal.h"

#define printf_debug(x)

static inline void parse_next_packet(
    ipt_worker_state *state, ipt_packet *packet 
);

void parse_section(
    u8* buffer, u64 start, u64 soft_end, u64 hard_end, int out_file
) {

}





/* ********* Packet Parsing ********* */
#define RETURN_IF_PARSED(x) \
    if (glue(parse_, x)(state, packet)) return

#define LEFT(n) \
    ((state->pos_in_buffer - state->buffer_size) >= n)

static inline bool parse_tip(ipt_worker_state *state, ipt_packet *packet);
static inline bool parse_psb(ipt_worker_state *state, ipt_packet *packet);
static inline bool parse_psbend(ipt_worker_state *state, ipt_packet *packet);


static inline void parse_next_packet(
    ipt_worker_state *state, ipt_packet *packet 
) {
    RETURN_IF_PARSED(tip);
    RETURN_IF_PARSED(psb);
    RETURN_IF_PARSED(psbend);

    packet->type = UNKOWN;
    packet->unkown_data.byte = state->buffer[
        state->pos_in_buffer++
    ];
}


static inline bool parse_psb(
    ipt_worker_state *state, ipt_packet *packet
) {
    static const u8 expected_buffer[] = PSB_PACKET_FULL;

    if (!LEFT(PSB_PACKET_LENGTH))
        return false;
    
    if (memcmp(
        state->buffer, expected_buffer, PSB_END_PACKET_LENGTH
    ) != 0) return false;

    packet->type = PSB;

    return true;
}


static inline bool parse_psbend(
    ipt_worker_state *state, ipt_packet *packet
) {
    if (!LEFT(PSB_END_PACKET_LENGTH))
        return false;

    const u8 *buffer = state->buffer;

    if (
        buffer[0] != OPPCODE_STARTING_BYTE ||
        buffer[1] != PSB_END_OPPCODE
    ) return false;

    packet->type = PSBEND;

    return true;
}


static inline bool parse_tip_type(
    const u8 *buffer, ipt_packet *packet
);

static inline bool ip_out_of_context(
    const u8 *buffer, ipt_packet *packet
);

static inline bool parse_amount_of_ip_in_tip(
    u8 ip_bits, u8 *amount_of_ip_in_tip
);

static inline void parse_ip_from_tip_buffer(
    ipt_worker_state *state, const u8 *buffer, 
    ipt_packet *packet, u8 amount_of_ip_in_tip
);


static inline bool parse_tip(
    ipt_worker_state *state, ipt_packet *packet
) {
    if (!LEFT(TIP_PACKET_LENGTH))
        return false;

    const u8* buffer = state->buffer;

    if (!parse_tip_type(buffer, packet))
        return false;

    u8 ip_bits = buffer[0] >> 5;

    if (ip_out_of_context(packet, ip_bits))
        return true; /* Not an invalid parsing, hence true */
    
    u8 amount_of_ip_in_tip = 0;

    if (!parse_amount_of_ip_in_tip(ip_bits, &amount_of_ip_in_tip))
        return false;

    parse_ip_from_tip_buffer(
        state, buffer, packet, amount_of_ip_in_tip
    );

    return false;
}


static inline bool parse_tip_type(
    const u8 *buffer, ipt_packet *packet
) {
    u8 bits = LOWER_BITS(
        buffer[0], TIP_OPPCODE_LENGTH_BITS
    );

    switch (bits) {
    case TIP_BASE_OPPCODE:
        packet->tip_data.type = TIP_TIP;
        return true;
    case TIP_PGE_OPPCODE:
        packet->tip_data.type = TIP_PGE;
        return true;
    case TIP_PGD_OPPCODE:
        packet->tip_data.type = TIP_PGD;
        return true;
    case TIP_FUP_OPPCODE:
        packet->tip_data.type = TIP_FUP;
        return true;
    default
        return false;
    }
}


static inline bool ip_out_of_context(
    ipt_packet *packet, u8 ip_bits
) {
    if (ip_bits == 0b000) {
        state->pos_in_buffer++;
        packet->type = TIP_OUT_OF_CONTEXT;
        return true;
    }

    return false;
}


static inline bool parse_amount_of_ip_in_tip(
    u8 ip_bits, u8 *amount_of_ip_in_tip
) {
    switch (ip_bits) {
    case 0b001:
        *amount_of_ip_in_tip = 6;
        return true;
    case 0b010:
        *amount_of_ip_in_tip = 4;
        return true;
    case 0b011:
        printf_debug("TIP - Not Implemented\n");
        return false;
    case 0b100:
        *amount_of_ip_in_tip = 2;
        return true;
    case 0b110:
        *amount_of_ip_in_tip = 0;
        return true;
    default:
        printf_debug("TIP - Reserved Bits\n");
        return false;
    }
}


static inline void parse_ip_from_tip_buffer(
    ipt_worker_state *state, const u8 *buffer, 
    ipt_packet *packet, u8 amount_of_ip_in_tip
) {
    u64 ip_buffer = 0;
    u64 ip = state->last_tip_value;

    for(int i = 0; i < 8; ++i) {
        u8 next_byte = 
            (i >= amount_of_ip_in_tip) ? 
                buffer[8 - i] : (curr_ip >> (7 - i) * 8) & 0xff;
        
        ip = (ip << 8) | byte;

        ip_buffer = ()
    }
}