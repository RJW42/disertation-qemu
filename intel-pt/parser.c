#include "intel-pt/parser-internal.h"
#include "intel-pt/internal-types.h"
#include "qemu/osdep.h"

#include <stdio.h>

static volatile IntelPTDataBuffer *buffer;

bool intel_pt_parsing_init(
    int version, volatile IntelPTDataBuffer* intel_pt_buffer
) {
    buffer = intel_pt_buffer;

    return true;
}

void intel_pt_parser_signal_qemu_end(void)
{

}

void intel_pt_parser_signal_recorder_end(void)
{
    printf("Recording Done!\n");
    printf("%lu\n", buffer->write_head);
}