#include <stdio.h>
#include <stdlib.h>

#include "ctrace.h"


static FILE *trace_dump = NULL;

void init_trace_gen(void) 
{
    printf("Start\n");

    // Open dump file
    trace_dump = fopen("trace-dump.txt", "w");
    
    if(trace_dump == NULL) {
         printf("Failed to open trace-dump.txt file");
         exit(1);
    }
}

void clean_trace_gen(void)
{
    printf("Finished\n");
    fclose(trace_dump);
}


void ctrace_basic_block(long guest_pc) 
{
    fprintf(trace_dump, "%lu\n", guest_pc);
}