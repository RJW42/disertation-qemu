/*
 * Interface for configuring and controlling the state of intel processor trace events.
 */

#include "qemu/osdep.h"
#include "intel-pt/control.h"
#include "intel-pt/record-internal.h"
#include "intel-pt/parser-internal.h"
#include "qemu/help_option.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "intel-pt/internal-types.h"

static int version;
static bool enabled;
static volatile IntelPTDataBuffer intel_pt_buffer;

QemuOptsList intel_pt_opts = {
    .name = "intel-pt",
    .implied_opt_name = "intel-pt",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(intel_pt_opts.head),
    .desc = {
        {   
            .name = "version",
            .type = QEMU_OPT_NUMBER,
        },
        { /* end of list */ }
    },
};


bool intel_pt_init(void)
{
    if (!enabled) return true;

    intel_pt_recording_init(version, &intel_pt_buffer);

    return true;
}


void intel_pt_signal_qemu_end(void)
{
    if (!enabled) return;

    intel_pt_parser_signal_qemu_end();
    intel_pt_recording_signal_qemu_end();
    intel_pt_parser_signal_recorder_end();
}


static bool intel_pt_parse_version_opt(const char* opt);

void intel_pt_opt_parse(const char *optarg)
{
    QemuOpts *opts = qemu_opts_parse_noisily(
        qemu_find_opts("intel-pt"), optarg, true
    );

    if (!opts) {
        exit(1);
    }

    enabled = true;

    const bool parsed_version = (
        qemu_opt_get(opts, "version") && 
        intel_pt_parse_version_opt(qemu_opt_get(
            opts, "version"
        ))
    );

    if (!parsed_version) {
        exit(1);
    }

    qemu_opts_del(opts);
}


static bool intel_pt_parse_version_opt(const char* opt) 
{
    if (opt[0] < '0' || opt[0] > '9') 
        return false;

    version = opt[0] - '0'; 

    if (version != 0)
        return false;

    return true;
}
