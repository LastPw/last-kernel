#ifndef COMPAT_H
#define COMPAT_H

#include "types.h"

typedef enum {
    COMPAT_PERS_LASTOS = 0,
    COMPAT_PERS_WINDOWS = 1,
    COMPAT_PERS_LINUX = 2
} compat_personality_t;

typedef struct {
    u16 arch;
    u16 ptr_size;
    u32 calling_conv;
    u32 errno_model;
} compat_abi_desc_t;

typedef struct {
    compat_personality_t personality;
    compat_abi_desc_t abi;
    u32 syscall_table_version;
    u32 memory_model_id;
    u32 exception_model_id;
} pal_descriptor_t;

typedef struct {
    u8 enabled;
    u8 tracked_processes;
    u8 cmc_count;
    u8 reserved;
    u32 fast_path_hits;
    u32 slow_path_hits;
    u32 abi_conflicts;
    u32 astg_promotions;
    u32 bbf_profiles;
    u32 sece_records;
    u32 quarantined_profiles;
    u32 prediction_warmups;
    u32 cache_entries;
    u32 dll_sync_events;
    u32 dll_desync_events;
    u32 dll_profiles;
    u32 dll_avg_sync_score;
    u32 bridge_fast_hits;
    u32 bridge_slow_hits;
    u32 bridge_low_confidence;
    u32 fallback_execs;
    u32 guard_blocks;
    u32 namespace_rebinds;
} compat_status_t;

typedef struct {
    u8 valid;
    compat_personality_t personality;
    u32 cmc_id;
    u32 bbf_id;
    u32 astg_hot;
    u32 exec_count;
    u32 crash_count;
    u32 predicted_mem_kb;
    u32 predicted_threads;
    u32 io_burst_score;
    u32 dll_sync_score;
    u32 dll_drift_score;
    u32 dll_bitmap_hits;
    u32 sync_score;
    u32 sync_failures;
    u32 bridge_confidence;
    u32 bridge_fast_hits;
    u32 bridge_slow_hits;
    u32 bridge_last_native;
    u32 guard_level;
} compat_profile_t;

typedef struct {
    u8 valid;
    u8 mode; /* 0: quick, 1: deep */
    u16 reserved;
    u32 task_id;
    compat_personality_t personality;
    u32 dll_sync_score_before;
    u32 dll_sync_score_after;
    u32 bridge_confidence_before;
    u32 bridge_confidence_after;
    u32 sampled_calls;
    u32 success_calls;
    u32 failed_calls;
    u32 predicted_mem_kb;
    u32 predicted_threads;
    u32 final_sync_score;
    u32 fallback_execs_before;
    u32 fallback_execs_after;
    u32 guard_blocks_before;
    u32 guard_blocks_after;
    u32 namespace_rebinds_before;
    u32 namespace_rebinds_after;
} compat_sync_report_t;

typedef enum {
    COMPAT_BIN_UNKNOWN = 0,
    COMPAT_BIN_PE32 = 1,
    COMPAT_BIN_PE64 = 2,
    COMPAT_BIN_ELF32 = 3,
    COMPAT_BIN_ELF64 = 4
} compat_binary_kind_t;

typedef struct {
    u8 valid;
    compat_binary_kind_t kind;
    compat_personality_t personality_hint;
    u32 arch_bits;
    u32 entry_point;
    u32 image_base_lo;
    u32 section_or_ph_count;
    u32 subsystem_hint;
    u32 abi_hint;
} compat_binary_probe_t;

void compat_init(void);
void compat_tick(void);
int compat_bind_task(u32 task_id, compat_personality_t personality, u32 bbf_seed);
compat_personality_t compat_personality_of(u32 task_id);
int compat_syscall(u32 task_id, u32 foreign_nr, u32 a0, u32 a1, u32 a2, u32* out_ret);
int compat_sync_task(u32 task_id, u32 mode, compat_sync_report_t* out);
u32 compat_autosync_step(u32 budget);
int compat_probe_binary(const u8* data, u32 len, compat_binary_probe_t* out);
int compat_prepare_task_from_probe(u32 task_id, const compat_binary_probe_t* probe);
int compat_prepare_task_from_kind(u32 task_id, compat_binary_kind_t kind);
void compat_status(compat_status_t* out);
int compat_profile(u32 task_id, compat_profile_t* out);
void compat_note_exec(u32 task_id);
void compat_note_crash(u32 task_id, u32 reason);
void compat_pal_descriptor(compat_personality_t p, pal_descriptor_t* out);

#endif
