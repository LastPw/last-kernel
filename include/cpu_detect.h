#ifndef CPU_DETECT_H
#define CPU_DETECT_H

#include "types.h"

typedef struct {
    char vendor[13];
    char brand[49];
    u32 family;
    u32 model;
    u32 stepping;
    u32 max_basic_leaf;
    u32 max_ext_leaf;
    u32 logical_cores;
    u32 physical_cores;
    u32 cache_line_bytes;
    u32 l2_cache_kb;
    u32 l3_cache_kb;
    u8 has_apic;
    u8 has_sse;
    u8 has_avx;
    u8 has_long_mode;
    u8 has_nx;
    u8 has_smep;
    u8 has_smap;
    u8 has_cpuid;
    u8 has_mwait;
    u8 has_power_mgmt;
    u8 has_tsc_deadline;
    u8 is_modern_profile;
} cpu_info_t;

void cpu_detect_init(void);
const cpu_info_t* cpu_get_info(void);

#endif
