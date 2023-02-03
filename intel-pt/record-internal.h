#ifndef INTEL_PT__RECORD_INTERNAL_H
#define INTEL_PT__RECORD_INTERNAL_H

#include "intel-pt/internal-types.h"

bool intel_pt_recording_init(
    int version, volatile IntelPTDataBuffer* intel_pt_buffer
);

void intel_pt_recording_signal_qemu_end(void);

#endif