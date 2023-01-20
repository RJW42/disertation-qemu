#ifndef INTEL_PT__PARSER_WORKER_H
#define INTEL_PT__PARSER_WORKER_H

#include "intel-pt/internal-types.h"

void parse_section(
    u8* buffer, u64 start, u64 end, int out_file
);

#endif