#include <stdio.h>
#include <stdlib.h>

#include "qemu/osdep.h"
#include "qemu/help_option.h"
#include "qemu/option.h"
#include "ctrace.h"


static FILE *trace_dump = NULL;
int pt_trace_version = 0;

QemuOptsList qemu_pt_trace_opts = {
    .name = "pt-trace",
    .implied_opt_name = "pt-trace",
    .merge_lists = true,
    .head = QTAILQ_HEAD_INITIALIZER(qemu_pt_trace_opts.head),
    .desc = {
        {/* end of list */}
    },
};

void init_trace_gen(void) 
{
    if(pt_trace_version == 0) {
        return;
    }

    // Open dump file
    trace_dump = fopen("trace-dump.txt", "w");
    
    if(trace_dump == NULL) {
         printf("Failed to open trace-dump.txt file");
         exit(1);
    }
}

void clean_trace_gen(void)
{
    printf("Finish\n");
    if(pt_trace_version == 0) {
        return;
    }
    
    fclose(trace_dump);
}


void ctrace_basic_block(long guest_pc) 
{
    fprintf(trace_dump, "%lu\n", guest_pc);
}


void pt_trace_opt_parse(const char *optarg)
{
    int trace_version = optarg[0] - '0';
    if(trace_version < 0 || trace_version > 2) {
        printf("Invalid pt-trace arg\n");
        exit(1);
    }
    pt_trace_version = trace_version + 1;

    // if(pt_trace_version == PT_TRACE_SOFTWARE_V1) {
    //     // Disable direct block chaining for version 1
    //     printf("nochain\n");
    //     setenv("QEMU_LOG", "nochain", 1);
    // }
}