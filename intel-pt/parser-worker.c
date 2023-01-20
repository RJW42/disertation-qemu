#include "intel-pt/parser-worker.h"
#include "intel-pt/internal-types.h"
#include "intel-pt/intel-pt-oppcodes.h"
#include "intel-pt/parser-worker-internal.h"

static inline void parse_next_packet(
    ipt_worker_state *state, ipt_packet *packet 
);

void parse_section(
    u8* buffer, u64 start, u64 end, int out_file
) {

}





/* ********* Packet Parsing ********* */
#define RETURN_IF_PARSED(x) \
    if parse_##x(state, packet) return

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


static inline bool parse_tip(
    ipt_worker_state *state, ipt_packet *packet
) {
    return false;
}


static inline bool parse_psb(
    ipt_worker_state *state, ipt_packet *packet
) {
    return false;
}


static inline bool parse_psbend(
    ipt_worker_state *state, ipt_packet *packet
) {
    return false;
}