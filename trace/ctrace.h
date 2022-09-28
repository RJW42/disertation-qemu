#ifndef TRACE__PT_TRACE_H_
#define TRACE__PT_TRACE_H_

#define PT_TRACE_SOFTWARE_V1 1
#define PT_TRACE_SOFTWARE_V2 2
#define PT_TRACE_HARDWARE_V1 3

/* 
 * Version of pt_trace set 
 */
extern int pt_trace_version;

/**
 * Definition of QEMU options describing the pt trace 
 */
extern QemuOptsList qemu_pt_trace_opts;

void init_trace_gen(void);
void clean_trace_gen(void);
void ctrace_basic_block(long guest_pc);
void pt_trace_opt_parse(const char *arg);

#endif