#ifndef TRACE__PT_TRACE_H_
#define TRACE__PT_TRACE_H_

/**
 * Definition of QEMU options describing the pt trace 
 */
extern QemuOptsList qemu_pt_trace_opts;

void init_trace_gen(void);
void clean_trace_gen(void);
void ctrace_basic_block(long guest_pc);
void pt_trace_opt_parse(const char *arg);

#endif