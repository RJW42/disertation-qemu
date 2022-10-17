#ifndef PARSE_INTERNAL_H_
#define PARSE_INTERNAL_H_

#include <stdbool.h>

static unsigned long get_mapping(unsigned long host_pc);

static void load_trace_file(char *file_name);
static void load_mapping_file(char *file_name);
static void parse();

static bool parse_tnt(void);
static bool parse_tip(void);
static bool parse_pip(void);
static bool parse_mode(void);
static bool parse_trace_stop(void);
static bool parse_cbr(void);
static bool parse_tsc(void);
static bool parse_mtc(void);
static bool parse_tma(void);
static bool parse_vmcs(void);
static bool parse_ovf(void);
static bool parse_cyc(void);
static bool parse_psb(void);
static bool parse_psb_end(void);
static bool parse_mnt(void);
static bool parse_pad(void);
static bool parse_ptw(void);
static bool parse_exstop(void);
static bool parse_mwait(void);
static bool parse_pwre(void);
static bool parse_pwrx(void);
static bool parse_bbp(void);
static bool parse_bip(void);
static bool parse_bep(void);
static bool parse_cfe(void);
static bool parse_evd(void);

static bool parse_tip_base(unsigned char *buffer);
static bool parse_tip_pge(unsigned char *buffer);
static bool parse_tip_pgd(unsigned char *buffer);
static bool parse_tip_fup(unsigned char *buffer);

typedef struct mapping_node {
    unsigned long key;
    unsigned long value;
    struct mapping_node *next;
} mapping_node;

#endif