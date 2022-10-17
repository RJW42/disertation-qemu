#include "parse-internal.h"
#include "parse-oppcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

static FILE* trace_data;
static mapping_node* mapping;
static unsigned long offset;
static unsigned long size;

//#define DEBUG_MODE_

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0') 


#define LEFT(n) ((size - offset) >= n) // TODO: Check this >=
#define ADVANCE(n) offset += n
#define REVERT(n) offset -= n
#define GET_BYTES(buffer, size) do {\
    fseek(trace_data, offset, SEEK_SET); \
    fread(buffer, size, 1, trace_data); \
}while(0)
#define INIT_BUFFER(name, size) \
    unsigned char name[size]; \
    GET_BYTES(name, size)

#define LOWER_BITS(value, n) (value & ((1 << n) - 1))
#define MIDDLE_BITS(value, uppwer, lower) (value & (((1 << uppwer) - 1) << lower))

int main() 
{
    // TODO: remove this used for testing 
    char trace_file_name[] = "trace-dump.pt";
    char mapping_file_name[] = "mapping-data.mpt";

    load_mapping_file(mapping_file_name);
    load_trace_file(trace_file_name);
    
    parse();
}

/* ***** Parsing ***** */
static unsigned long last_ip = 0;

void parse(void) 
{

#ifdef DEBUG_MODE_
    printf("Size: %lu\n", size);
#endif

    while(offset < size) {
        if(parse_tnt()) {
            continue;
        } 
        else if(parse_tip()) {
            continue;
        }
        else if(parse_mode()) {
            continue;
        }
        else if(parse_trace_stop()){
            continue;
        }
        else if(parse_cbr()) {
            continue;
        }
        else if(parse_tsc()) {
            continue;
        }
        else if(parse_mtc()) {
            continue;
        }
        else if(parse_tma()) {
            continue;
        }
        else if(parse_vmcs()) {
            continue;
        }
        else if(parse_ovf()) {
            continue;
        }
        else if(parse_cyc()) {
            continue;
        }
        else if(parse_psb()) {
            continue;
        }
        else if(parse_psb_end()) {
            continue;
        }
        else if(parse_mnt()) {
            continue;
        }
        else if(parse_pad()) {
            continue;
        }
        else if(parse_ptw()) {
            continue;
        }
        else if(parse_exstop()) {
            continue;
        }
        else if(parse_mwait()) {
            continue;
        }
        else if(parse_pwre()) {
            continue;
        }
        else if(parse_pwre()) {
            continue;
        }
        else if(parse_pwrx()) {
            continue;
        }
        else if(parse_bbp()) {
            continue;
        }
        else if(parse_bip()) {
            continue;
        }
        else if(parse_bep()) {
            continue;
        }
        else if(parse_cfe()) {
            continue;
        }
        else if(parse_cfe()) {
            continue;
        }
        else if(parse_evd()) {
            continue;
        }

        unsigned char byte;
        GET_BYTES(&byte, 1);
        ADVANCE(1);

#ifdef DEBUG_MODE_
        fprintf(stdout, "Unkown: "BYTE_TO_BINARY_PATTERN"\n", BYTE_TO_BINARY(byte));
#endif
    }
}


static bool parse_tnt(void) 
{
    // Attempt to pase a short TNT packet
    if(!LEFT(SHORT_TNT_PACKET_LENGTH))
        return false;
    
    char byte;
    GET_BYTES(&byte, 1);

    if(LOWER_BITS(byte, 1) == 0) {
        for(int i = 6; i > 0; i--) {
            if(byte & (2 << i)) {
#ifdef DEBUG_MODE_
                printf("SHORT TNT: "BYTE_TO_BINARY_PATTERN"\n", BYTE_TO_BINARY(byte));
#endif
                ADVANCE(1);
                return true;
            }
        }
    }

    // Attempt to parse a Long TNT packet 
    if(!LEFT(LONG_TNT_PACKET_LENGTH)) 
        return false;

    INIT_BUFFER(buffer, LONG_TNT_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE || 
       buffer[1] != LONG_TNT_OPPCODE) 
        return false;

    ADVANCE(LONG_TNT_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("LONG TNT\n");
#endif

    return true;
}


static bool parse_tip(void) 
{
    if(!LEFT(TIP_PACKET_LENGTH))
        return false;

    INIT_BUFFER(buffer, TIP_PACKET_LENGTH);

    // Get Type
    if(parse_tip_base(buffer)) {
#ifdef DEBUG_MODE_ 
        printf("TIP - ");
#endif
    } else if(parse_tip_pge(buffer)) {
#ifdef DEBUG_MODE_ 
        printf("TIP PGE - ");
#endif
    } else if(parse_tip_pgd(buffer)) {
#ifdef DEBUG_MODE_ 
        printf("TIP PGD - ");
#endif
    } else if(parse_tip_fup(buffer)) {
#ifdef DEBUG_MODE_ 
        printf("TIP FUP - ");
#endif
    } else {
        return false;
    }


    // Check compressed status 
    unsigned char ip_bits = buffer[0] >> 5;
    unsigned char last_ip_use = 0;

#ifdef DEBUG_MODE_
    printf(BYTE_TO_BINARY_PATTERN" - ", BYTE_TO_BINARY(buffer[0]));
#endif

    switch (ip_bits)
    {
    case 0b000:
#ifdef DEBUG_MODE
        printf("IP out of context\n");
#endif
        return false;
    case 0b001:
        last_ip_use = 6;
        break;
    case 0b010:
        last_ip_use = 4;
        break;
    case 0b011:
#ifdef DEBUG_MODE_ 
        printf("Not implemented ");
#endif
        return false;
        break;
    case 0b100:
        last_ip_use = 2;
        break;
    case 0b110:
        last_ip_use = 0;
        break;
    default:
#ifdef DEBUG_MODE_ 
        printf("Reserved bits\n");
#endif
        return false;
    }

#ifdef DEBUG_MODE_
    printf("%u - ", last_ip_use);
#endif

    // Create ip
    unsigned long ip = 0;

    for(int i = 0; i < 8; i++) {
        unsigned char byte; 

        if(i >= last_ip_use) {
            byte = buffer[8 - i];
        } else {
            byte = (last_ip >> ((7 - i) * 8)) & 0xff;
        }

        ip = (ip << 8) | byte;
    }

#ifdef DEBUG_MODE_ 
    printf("IP: %lu\n", ip);
#endif

    last_ip = ip;

    unsigned long guest_ip = get_mapping(last_ip);

    //printf("%lu\n", guest_ip);

    ADVANCE(TIP_PACKET_LENGTH - last_ip_use);

    return true;
}  


static bool parse_tip_base(unsigned char *buffer) 
{
    unsigned char bits = LOWER_BITS(buffer[0], TIP_OPPCODE_LENGTH_BITS);

    if(bits != TIP_BASE_OPPCODE)
        return false;
    return true;
}


static bool parse_tip_pge(unsigned char *buffer) 
{
    unsigned char bits = LOWER_BITS(buffer[0], TIP_OPPCODE_LENGTH_BITS);

    if(bits != TIP_PGE_OPPCODE)
        return false;
    return true;
}


static bool parse_tip_pgd(unsigned char *buffer) 
{
    unsigned char bits = LOWER_BITS(buffer[0], TIP_OPPCODE_LENGTH_BITS);

    if(bits != TIP_PGD_OPPCODE)
        return false;
    return true;
}


static bool parse_tip_fup(unsigned char *buffer) 
{
    unsigned char bits = LOWER_BITS(buffer[0], TIP_OPPCODE_LENGTH_BITS);

    if(bits != TIP_FUP_OPPCODE)
        return false;
    return true;
}


static bool parse_pip(void)
{
    if(!LEFT(PIP_PACKET_LENGTH))
        return false;
    
    INIT_BUFFER(buffer, PIP_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE ||
       buffer[1] != PIP_OPPCODE)
        return false;

    ADVANCE(PIP_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("PIP\n");
#endif

    return true;
}


static bool parse_mode(void)
{
    if(!LEFT(MODE_PACKET_LENGTH))    
        return false;
    
    INIT_BUFFER(buffer, MODE_PACKET_LENGTH);

    if(buffer[0] != MODE_OPPCODE)
        return false;

    // Todo: Parse the two different types of mode

    ADVANCE(MODE_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("MODE\n");
#endif

    return true;
}


static bool parse_trace_stop(void) 
{
    if(!LEFT(TRACE_STOP_PACKET_LENGTH))
        return false;

    INIT_BUFFER(buffer, TRACE_STOP_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE || 
       buffer[1] != TRACE_STOP_OPPCODE)
        return false;
    
    ADVANCE(TRACE_STOP_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("Trace Stop\n");
#endif

    return true;
}


static bool parse_cbr(void) 
{
    if(!LEFT(CBR_PACKET_LENGTH))
        return false;

    INIT_BUFFER(buffer, CBR_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE || 
       buffer[1] != CBR_OPPCODE)
        return false;

    ADVANCE(CBR_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("CBR\n");
#endif

    return true;
}


static bool parse_tsc(void) 
{
    if(!LEFT(TSC_PACKET_LENGTH))
        return false;

    INIT_BUFFER(buffer, TSC_PACKET_LENGTH);

    if(buffer[0] != TSC_OPPCODE)
        return false;

    ADVANCE(TSC_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("TSC\n");
#endif

    return true;
}


static bool parse_mtc(void)
{
    if(!LEFT(MTC_PACKET_LENGTH))
        return false;

    INIT_BUFFER(buffer, MTC_PACKET_LENGTH);

    if(buffer[0] != MTC_OPPCODE)
        return false;

    ADVANCE(TSC_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("MTC\n");
#endif

    return true;
}


static bool parse_tma(void)
{
    if(!LEFT(TMA_PACKET_LENGTH))
        return false;

    INIT_BUFFER(buffer, TMA_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE || 
       buffer[1] != TMA_OPPCODE) 
        return false;
    
    ADVANCE(TMA_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("TMA\n");
#endif

    return true;
}


static bool parse_vmcs(void)
{
    if(!LEFT(VMCS_PACKET_LENGTH))
        return false;

    INIT_BUFFER(buffer, VMCS_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE || 
       buffer[1] != VMCS_OPPCODE)
        return false;

    ADVANCE(VMCS_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("VMCS\n");
#endif

    return true;
}


static bool parse_ovf(void)
{
    if(!LEFT(OVF_PACKET_LENGTH))
        return false;

    INIT_BUFFER(buffer, OVF_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE || 
       buffer[1] != OVF_OPPCODE)
        return false;

    ADVANCE(OVF_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("OVF\n");
#endif

    return true;
}


static bool parse_cyc(void)
{
    // Todo: implement this
    return false;
}


static bool parse_psb(void)
{
    if(!LEFT(PSB_PACKET_LENGTH))
        return false;

    INIT_BUFFER(buffer, PSB_PACKET_LENGTH);

    char expected_buffer[] = PSB_PACKET_FULL;

    if(memcmp(buffer, expected_buffer, PSB_PACKET_LENGTH) != 0)
        return false;

    ADVANCE(PSB_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("PSB\n");
#endif

    // Todo: Unsure if this is correct. 
    last_ip = 0;

    return true;
}


static bool parse_psb_end(void)
{
    if(!LEFT(PSB_END_PACKET_LENGTH))
        return false;

    INIT_BUFFER(buffer, PSB_END_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE || 
       buffer[1] != PSB_END_OPPCODE)
        return false;

    ADVANCE(PSB_END_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("PSBEND\n");
#endif

    return true;
}


static bool parse_mnt(void)
{
    if(!LEFT(MNT_PACKET_LENGTH))
        return false;

    INIT_BUFFER(buffer, MNT_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE || 
       buffer[1] != MNT_OPPCODE_1 || 
       buffer[2] != MNT_OPPCODE_2)
        return false;

    ADVANCE(MNT_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("MNT\n");
#endif

    return true;
}


static bool parse_pad(void)
{
    if(!LEFT(PAD_PACKET_LENGTH))   
        return false;

    INIT_BUFFER(buffer, PAD_PACKET_LENGTH);

    if(buffer[0] != PAD_OPPCODE)
        return false;

    ADVANCE(PAD_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("PAD\n");
#endif

    return true;
}


static bool parse_ptw(void) 
{
    if(!LEFT(PTW_HEADER_LENGTH))
        return false;

    INIT_BUFFER(header, PTW_HEADER_LENGTH);

    if(header[0] != OPPCODE_STARTING_BYTE && 
       LOWER_BITS(header[1], 5) != PTW_OPPCODE)
        return false;

    unsigned char payload_bits = MIDDLE_BITS(header[1], 7, 5);

    if(payload_bits != PTW_L1_CODE && payload_bits != PTW_L2_CODE)
        return false;

    unsigned char packet_length = PTW_HEADER_LENGTH + 
        (payload_bits == PTW_L1_CODE) ? PTW_BODY_LENGTH_1 : PTW_BODY_LENGTH_2;

    ADVANCE(packet_length);

#ifdef DEBUG_MODE_
    printf("PTW\n");
#endif    

    return true;
}


static bool parse_exstop(void)
{
    if(!LEFT(EXSTOP_PACKET_LENGTH))
        return false;

    INIT_BUFFER(buffer, EXSTOP_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE ||
       LOWER_BITS(buffer[1], 7) != EXSTOP_OPPCODE)
        return false;

    ADVANCE(EXSTOP_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("EXSTOP\n");
#endif

    return true;
}


static bool parse_mwait(void)
{
    if(!LEFT(MWAIT_PACKET_LENGTH))
        return false;
    
    INIT_BUFFER(buffer, MWAIT_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE ||
       buffer[1] != MWAIT_OPPCODE)
        return false;

    ADVANCE(MWAIT_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("MWAIT\n");
#endif

    return true;
}


static bool parse_pwre(void)
{
    if(!LEFT(PWRE_PACKET_LENGTH))
        return false;
    
    INIT_BUFFER(buffer, PWRE_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE ||
       buffer[1] != PWRE_OPPCODE)
        return false;

    ADVANCE(PWRE_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("PWRE\n");
#endif

    return true;
}


static bool parse_pwrx(void)
{
    if(!LEFT(PWRX_PACKET_LENGTH))
        return false;
    
    INIT_BUFFER(buffer, PWRX_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE ||
       buffer[1] != PWRX_OPPCODE)
        return false;

    ADVANCE(PWRX_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("PWRX\n");
#endif

    return true;
}


static bool parse_bbp(void)
{
    if(!LEFT(BBP_PACKET_LENGTH))
        return false;
    
    INIT_BUFFER(buffer, BBP_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE ||
       buffer[1] != BBP_OPPCODE)
        return false;

    ADVANCE(BBP_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("BBP\n");
#endif

    return true;
}


static bool parse_bip(void)
{
    // Todo implement
    return false;
}


static bool parse_bep(void)
{
    if(!LEFT(BEP_PACKET_LENGTH))
        return false;

    INIT_BUFFER(buffer, BEP_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE ||
       LOWER_BITS(buffer[1], 7) != BEP_OPPCODE)
        return false;

    ADVANCE(BEP_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("BEP\n");
#endif

    return true;
}


static bool parse_cfe(void)
{
    if(!LEFT(CFE_PACKET_LENGTH))
        return false;
    
    INIT_BUFFER(buffer, CFE_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE ||
       buffer[1] != CFE_OPPCODE)
        return false;

    ADVANCE(CFE_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("CFE\n");
#endif

    return true;
}


static bool parse_evd(void)
{
    if(!LEFT(EVD_PACKET_LENGTH))
        return false;
    
    INIT_BUFFER(buffer, EVD_PACKET_LENGTH);

    if(buffer[0] != OPPCODE_STARTING_BYTE ||
       buffer[1] != EVD_OPPCODE)
        return false;

    ADVANCE(EVD_PACKET_LENGTH);

#ifdef DEBUG_MODE_
    printf("EVD\n");
#endif

    return true;
}


/* ***** File Management ***** */
static void load_trace_file(char *file_name)
{
    trace_data = fopen(file_name, "rb");

    if(trace_data == NULL) {
        fprintf(stderr, "Failed to open data file: %s\n", file_name);
        exit(EXIT_FAILURE);
    }

    fseek(trace_data, 0L, SEEK_END);
    size = ftell(trace_data);
    fseek(trace_data, 0L, SEEK_SET);

    offset = 0;
}


static void load_mapping_file(char *file_name) 
{
    FILE* mapping_data = fopen(file_name, "r");

    if(mapping_data == NULL) {
        fprintf(stderr, "Failed to open data file: %s\n", file_name);
        exit(EXIT_FAILURE);
    }

    unsigned long guest_pc;
    unsigned long host_pc;

    mapping = NULL;

    while(fscanf(mapping_data, "%lu, %lu\n", &guest_pc, &host_pc) != EOF) {
        //printf("%lu, %lu\n", guest_pc, host_pc);
        mapping_node *n = (mapping_node*)malloc(sizeof(mapping_node));
        n->key = host_pc;
        n->value = guest_pc;
        n->next = mapping;
        mapping = n;
    }
}


static unsigned long get_mapping(unsigned long host_pc) 
{
    unsigned long output = 0;
    for(mapping_node *tmp = mapping; tmp != NULL; tmp = tmp->next){
        if(tmp->key == host_pc) {
            output = tmp->value;
        } 
    }
    if(output != 0) printf("%lu\n", output);
    return output;
}