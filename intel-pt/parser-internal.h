#ifndef INTEL_PT__PARSER_INTERNAL_H
#define INTEL_PT__PARSER_INTERNAL_H

#include "intel-pt/internal-types.h"

void intel_pt_parsing_init(
    int version, volatile IntelPTDataBuffer* intel_pt_buffer
);

void intel_pt_parser_signal_qemu_end(void);

void intel_pt_parser_signal_recorder_end(void);

#endif