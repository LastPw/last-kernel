#ifndef SELF_HEAL_H
#define SELF_HEAL_H

#include "types.h"
#include "interrupts.h"

#define KSHIELD_MAX_CHECKPOINTS 8u
#define KSHIELD_MAX_TRACKED_TASKS 64u
#define KSHIELD_SHADOW_PAGE_SIZE 4096u

typedef struct {
    u64 rip;
    u64 rsp;
    u64 rbp;
    u64 rflags;
    u64 rax, rbx, rcx, rdx;
    u64 rsi, rdi;
    u64 r8, r9, r10, r11, r12, r13, r14, r15;
    u64 cr3;
    u32 tick;
    u8  valid;
    u8  fxsave_area[512] __attribute__((aligned(16)));
    u64 shadow_stack_va;
    u8  shadow_stack_data[KSHIELD_SHADOW_PAGE_SIZE];
    u8  shadow_has_memory;
} kshield_checkpoint_t;

typedef struct {
    u32 task_id;
    u32 head_idx;
    u32 heal_count;
    u32 memory_rollbacks;
    u32 fpu_restores;
    u32 last_fault_int;
    u64 last_fault_addr;
    u64 last_fault_rip;
    u32 last_heal_duration_us;
    kshield_checkpoint_t checkpoints[KSHIELD_MAX_CHECKPOINTS];
} kshield_task_tracker_t;

typedef struct {
    u32 total_heals;
    u32 memory_rollbacks;
    u32 fpu_restores;
    u32 page_fault_heals;
    u32 opcode_heals;
    u32 gpf_heals;
    u32 div_zero_heals;
    u32 active_tracked_tasks;
} kshield_stats_t;

void self_heal_init(void);
void self_heal_tick(void);
u32  self_heal_restart_count(void);
u32  self_heal_last_reason(void);

/* KShield Full-State (Memory + FPU + Registers) Time-Travel API */
void kshield_save_checkpoint(u32 task_id, const registers_t* regs);
int  kshield_try_heal(registers_t* regs);
void kshield_get_stats(kshield_stats_t* out);
u32  kshield_get_total_heals(void);

#endif /* SELF_HEAL_H */
