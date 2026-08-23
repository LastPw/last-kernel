#ifndef COMPAT_ENGINE_H
#define COMPAT_ENGINE_H

#include "compat.h"
#include "types.h"

void compat_engine_init(void);
void compat_engine_tick(void);
int compat_engine_bind(u32 task_id, compat_personality_t personality, u32 bbf_seed);
compat_personality_t compat_engine_personality_of(u32 task_id);
int compat_engine_syscall(u32 task_id, u32 foreign_nr, u32 a0, u32 a1, u32 a2, u32* out_ret);
int compat_engine_sync_task(u32 task_id, u32 mode, compat_sync_report_t* out);
u32 compat_engine_autosync_step(u32 budget);
int compat_engine_probe_binary(const u8* data, u32 len, compat_binary_probe_t* out);
int compat_engine_prepare_task_from_probe(u32 task_id, const compat_binary_probe_t* probe);
int compat_engine_prepare_task_from_kind(u32 task_id, compat_binary_kind_t kind);
void compat_engine_status(compat_status_t* out);
int compat_engine_profile(u32 task_id, compat_profile_t* out);
void compat_engine_note_exec(u32 task_id);
void compat_engine_note_crash(u32 task_id, u32 reason);
void compat_engine_pal_descriptor(compat_personality_t p, pal_descriptor_t* out);

#endif
