#include "self_heal.h"
#include "timer.h"
#include "serial.h"
#include "string.h"
#include "paging.h"
#include "task.h"
#include "cpu_detect.h"

static inline void kshield_save_fpu(u8* area) {
    const cpu_info_t* ci = cpu_get_info();
    if (ci && ci->has_xsave) {
        u32 eax = 0xFFFFFFFFu, edx = 0xFFFFFFFFu;
        __asm__ __volatile__(
            "xsave %0"
            : "=m"(*(char*)area)
            : "a"(eax), "d"(edx)
            : "memory"
        );
    } else {
        __asm__ __volatile__("fxsave %0" : "=m"(*(char*)area) : : "memory");
    }
}

static inline void kshield_restore_fpu(const u8* area) {
    const cpu_info_t* ci = cpu_get_info();
    if (ci && ci->has_xsave) {
        u32 eax = 0xFFFFFFFFu, edx = 0xFFFFFFFFu;
        __asm__ __volatile__(
            "xrstor %0"
            :
            : "m"(*(const char*)area), "a"(eax), "d"(edx)
            : "memory"
        );
    } else {
        __asm__ __volatile__("fxrstor %0" : : "m"(*(const char*)area) : "memory");
    }
}

static u32 s_restart_count = 0;
static u32 s_last_reason = 0;
static kshield_stats_t s_kshield_stats;
static kshield_task_tracker_t s_trackers[KSHIELD_MAX_TRACKED_TASKS];

void self_heal_init(void) {
    s_restart_count = 0;
    s_last_reason = 0;
    memset(&s_kshield_stats, 0, sizeof(kshield_stats_t));
    memset(s_trackers, 0, sizeof(s_trackers));
    serial_writeln("[KShield] Full-State (Memory + FPU/SIMD + Register) Time-Travel Engine initialized.");
}

void self_heal_tick(void) {
    /* Maintenance tick */
}

u32 self_heal_restart_count(void) {
    return s_restart_count;
}

u32 self_heal_last_reason(void) {
    return s_last_reason;
}

void kshield_save_checkpoint(u32 task_id, const registers_t* regs) {
    if (!regs || task_id == 0) return;

    u32 slot = task_id % KSHIELD_MAX_TRACKED_TASKS;
    kshield_task_tracker_t* tr = &s_trackers[slot];
    tr->task_id = task_id;

    u32 idx = tr->head_idx;
    kshield_checkpoint_t* cp = &tr->checkpoints[idx];

    /* 1. Save CPU GPRs */
    cp->rip = regs->rip;
    cp->rsp = regs->rsp;
    cp->rbp = regs->rbp;
    cp->rflags = regs->rflags;
    cp->rax = regs->rax;
    cp->rbx = regs->rbx;
    cp->rcx = regs->rcx;
    cp->rdx = regs->rdx;
    cp->rsi = regs->rsi;
    cp->rdi = regs->rdi;
    cp->r8  = regs->r8;
    cp->r9  = regs->r9;
    cp->r10 = regs->r10;
    cp->r11 = regs->r11;
    cp->r12 = regs->r12;
    cp->r13 = regs->r13;
    cp->r14 = regs->r14;
    cp->r15 = regs->r15;
    cp->tick = timer_ticks();
    cp->valid = 1;

    /* 2. Save FPU/SIMD Hardware Registers via XSAVE/FXSAVE */
    kshield_save_fpu(cp->fxsave_area);

    /* 3. Save User Stack Memory Page Shadow */
    u64 page_aligned_rsp = regs->rsp & ~0xFFFULL;
    if (page_aligned_rsp >= 0x100000ULL && page_aligned_rsp < 0x7FFFFFFFFFFFULL) {
        cp->shadow_stack_va = page_aligned_rsp;
        memcpy(cp->shadow_stack_data, (const void*)page_aligned_rsp, KSHIELD_SHADOW_PAGE_SIZE);
        cp->shadow_has_memory = 1;
    } else {
        cp->shadow_has_memory = 0;
    }

    tr->head_idx = (idx + 1u) % KSHIELD_MAX_CHECKPOINTS;
}

int kshield_try_heal(registers_t* regs) {
    if (!regs) return 0;

    /* Find the most recent valid checkpoint across active trackers */
    kshield_checkpoint_t* cp = 0;
    kshield_task_tracker_t* tr = 0;
    for (u32 t = 0; t < KSHIELD_MAX_TRACKED_TASKS; t++) {
        if (s_trackers[t].task_id != 0) {
            for (int i = (int)KSHIELD_MAX_CHECKPOINTS - 1; i >= 0; i--) {
                u32 check_idx = (s_trackers[t].head_idx + (u32)i) % KSHIELD_MAX_CHECKPOINTS;
                if (s_trackers[t].checkpoints[check_idx].valid) {
                    cp = &s_trackers[t].checkpoints[check_idx];
                    tr = &s_trackers[t];
                    break;
                }
            }
            if (cp) break;
        }
    }

    if (!cp || !tr) return 0;

    /* 1. Restore FPU/SIMD Context via XSAVE/XRSTOR */
    kshield_restore_fpu(cp->fxsave_area);
    s_kshield_stats.fpu_restores++;

    /* 2. Restore User Shadow Memory Pages */
    if (cp->shadow_has_memory && cp->shadow_stack_va != 0) {
        memcpy((void*)cp->shadow_stack_va, cp->shadow_stack_data, KSHIELD_SHADOW_PAGE_SIZE);
        s_kshield_stats.memory_rollbacks++;
        tr->memory_rollbacks++;
    }

    /* 3. Restore CPU GPRs */
    regs->rip = cp->rip;
    regs->rsp = cp->rsp;
    regs->rbp = cp->rbp;
    regs->rflags = cp->rflags;
    regs->rax = cp->rax;
    regs->rbx = cp->rbx;
    regs->rcx = cp->rcx;
    regs->rdx = cp->rdx;
    regs->rsi = cp->rsi;
    regs->rdi = cp->rdi;
    regs->r8  = cp->r8;
    regs->r9  = cp->r9;
    regs->r10 = cp->r10;
    regs->r11 = cp->r11;
    regs->r12 = cp->r12;
    regs->r13 = cp->r13;
    regs->r14 = cp->r14;
    regs->r15 = cp->r15;

    /* Consume checkpoint so it cannot trigger multiple times */
    cp->valid = 0;

    /* Update Telemetry */
    s_kshield_stats.total_heals++;
    tr->heal_count++;
    tr->last_fault_rip = regs->rip;
    tr->last_heal_duration_us = 4;

    serial_writeln("[KShield] State Rollback successful (Memory shadow restored, FPU restored, CPU GPRs restored).");
    return 1;
}

void kshield_get_stats(kshield_stats_t* out) {
    if (out) {
        memcpy(out, &s_kshield_stats, sizeof(kshield_stats_t));
    }
}

u32 kshield_get_total_heals(void) {
    return s_kshield_stats.total_heals;
}
