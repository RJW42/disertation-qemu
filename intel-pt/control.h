/*
 * Interface for configuring and controlling the state of intel processor trace events.
 */

#ifndef INTEL_PT__CONTROL_H
#define INTEL_PT__CONTROL_H

#include "qemu/osdep.h"
#include "qemu/option.h"

/**
 * Definition of QEMU options describing intel pt subsystem configuration
 */
extern QemuOptsList intel_pt_opts;


/*
 * intel_pt_init
 *
 * Initilises the intel processing trace backends
 */
bool intel_pt_init(void);

/*
 * intel_pt_opt_parse
 *
 * Parse the intel pt command line arguments
 */
void intel_pt_opt_parse(const char *optarg);


/*
 * Signal to the intel pt backends that qemu has finished execution
 */
void intel_pt_signal_qemu_end(void);

#endif