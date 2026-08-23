#include "rcu.h"
#include "spinlock.h"
#include "timer.h"
#include "string.h"

#define RCU_MAX_CPUS 16u

typedef struct {
    u32 nesting;
    u64 last_quiescent_tick;
} rcu_percpu_t;

static rcu_percpu_t g_rcu_cpu[RCU_MAX_CPUS];
static spinlock_t g_rcu_lock;
static rcu_head_t* g_rcu_callbacks = 0;
static u64 g_rcu_completed_batches = 0;

void rcu_init(void) {
    spinlock_init(&g_rcu_lock);
    memset(g_rcu_cpu, 0, sizeof(g_rcu_cpu));
    g_rcu_callbacks = 0;
    g_rcu_completed_batches = 1;
}

void rcu_read_lock(void) {
    // In uniprocessor/preemption-disabled kernel, increment read nesting
    u32 cpu = 0; // core 0
    g_rcu_cpu[cpu].nesting++;
}

void rcu_read_unlock(void) {
    u32 cpu = 0;
    if (g_rcu_cpu[cpu].nesting > 0) {
        g_rcu_cpu[cpu].nesting--;
        if (g_rcu_cpu[cpu].nesting == 0) {
            g_rcu_cpu[cpu].last_quiescent_tick = timer_ticks();
        }
    }
}

void synchronize_rcu(void) {
    u64 target_tick = timer_ticks() + 2u;
    while ((i32)(timer_ticks() - target_tick) < 0) {
        __asm__ __volatile__("pause");
    }
    spinlock_lock(&g_rcu_lock);
    g_rcu_completed_batches++;
    spinlock_unlock(&g_rcu_lock);
}

void call_rcu(rcu_head_t* head, void (*func)(rcu_head_t* head)) {
    if (!head || !func) return;
    head->func = func;
    spinlock_lock(&g_rcu_lock);
    head->next = g_rcu_callbacks;
    g_rcu_callbacks = head;
    spinlock_unlock(&g_rcu_lock);
}

u64 rcu_batch_completed(void) {
    return g_rcu_completed_batches;
}
