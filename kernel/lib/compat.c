#include "compat.h"
#include "compat_engine.h"

void compat_init(void) {
    compat_engine_init();
}

void compat_tick(void) {
    compat_engine_tick();
}

int compat_bind_task(u32 task_id, compat_personality_t personality, u32 bbf_seed) {
    return compat_engine_bind(task_id, personality, bbf_seed);
}

compat_personality_t compat_personality_of(u32 task_id) {
    return compat_engine_personality_of(task_id);
}

int compat_syscall(u32 task_id, u32 foreign_nr, u32 a0, u32 a1, u32 a2, u32* out_ret) {
    return compat_engine_syscall(task_id, foreign_nr, a0, a1, a2, out_ret);
}

int compat_sync_task(u32 task_id, u32 mode, compat_sync_report_t* out) {
    return compat_engine_sync_task(task_id, mode, out);
}

u32 compat_autosync_step(u32 budget) {
    return compat_engine_autosync_step(budget);
}

int compat_probe_binary(const u8* data, u32 len, compat_binary_probe_t* out) {
    return compat_engine_probe_binary(data, len, out);
}

int compat_prepare_task_from_probe(u32 task_id, const compat_binary_probe_t* probe) {
    return compat_engine_prepare_task_from_probe(task_id, probe);
}

int compat_prepare_task_from_kind(u32 task_id, compat_binary_kind_t kind) {
    return compat_engine_prepare_task_from_kind(task_id, kind);
}

void compat_status(compat_status_t* out) {
    compat_engine_status(out);
}

int compat_profile(u32 task_id, compat_profile_t* out) {
    return compat_engine_profile(task_id, out);
}

void compat_note_exec(u32 task_id) {
    compat_engine_note_exec(task_id);
}

void compat_note_crash(u32 task_id, u32 reason) {
    compat_engine_note_crash(task_id, reason);
}

void compat_pal_descriptor(compat_personality_t p, pal_descriptor_t* out) {
    compat_engine_pal_descriptor(p, out);
}
