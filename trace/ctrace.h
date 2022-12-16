#ifndef TRACE__PT_TRACE_H_
#define TRACE__PT_TRACE_H_

#define PT_TRACE_SOFTWARE_V1 1
#define PT_TRACE_SOFTWARE_V2 2
#define PT_TRACE_HARDWARE_V1 3
#define PT_TRACE_HARDWARE_V2 4
#define PT_TRACE_HARDWARE_V3 5
#define PT_TRACE_HARDWARE_V4 6

/* 
 * Version of pt_trace set 
 */
extern int pt_trace_version;

/*
 * How long direct chaining should be allowed 
 * to continue before stopping 
 */
extern int pt_chain_count_limit;

/*
 * Used for logging asm
 * Didn't use functions as the imports are too messy
 */
extern FILE* pt_asm_log_file;

/**
 * Definition of QEMU options describing the pt trace 
 */
extern QemuOptsList qemu_pt_trace_opts;
extern QemuOptsList qemu_pt_loc_opts;

/*
 * If the first breakpoint call has been set 
 */
extern int set_pt_branchpoint_call;

void init_trace_gen(void);
void clean_trace_gen(void);
void ctrace_basic_block(long guest_pc);
void ctrace_record_mapping(long guest_pc, long host_pc);
void pt_trace_opt_parse(const char *arg);
void pt_trace_loc_opt_parse(const char *optarg);

void ipt_trace_enter(void);
void ipt_trace_exit(void);
void ipt_trace_exception_exit(void);
void ipt_trace_exception_enter(void);

void ipt_helper_enter(void);
void ipt_helper_exit(void);

void ipt_breakpoint_call(void);

#endif