#ifndef PROC_CORE_H
#define PROC_CORE_H

#include "types.h"

#define PROC_MAX_TASKS 4096u

typedef enum {
    PROC_SIG_NONE = 0,
    PROC_SIG_TERM = 15,
    PROC_SIG_KILL = 9,
    PROC_SIG_USR1 = 10,
    PROC_SIG_USR2 = 12
} proc_signal_t;

/* Signal handler descriptor — user-space fills this via sigaction. */
#define SIG_HANDLER_MAX 4u   /* NONE, KILL, TERM, USR1, USR2 */
#define SIG_FLAG_NONE    0u

typedef struct {
    u32 handler_addr;   /* user-space handler address (0 = default) */
    u32 flags;          /* reserved */
    u32 mask[2];        /* signal mask during handler (bit 0-63) */
} sigaction_t;

typedef struct {
    u32 task_id;
    u32 parent_task;
    u8 alive;
    u8 zombie;
    u16 pending_signals;
    i32 exit_code;
    u32 last_signal;
    u32 waiters;
} proc_status_t;

/* Register a signal handler for a task.  sig is 1-based (KILL=1, TERM=2, ...). */
int proc_core_sigaction(u32 task_id, u32 sig, u32 handler_addr);
u32 proc_core_sigaction_get(u32 task_id, u32 sig);

/* Convert internal proc_signal_t enum value to 1-based slot for sigaction API. */
static inline u32 proc_signal_to_slot(proc_signal_t sig) {
    switch (sig) {
        case PROC_SIG_KILL: return 1u;
        case PROC_SIG_TERM: return 2u;
        case PROC_SIG_USR1: return 3u;
        case PROC_SIG_USR2: return 4u;
        default:             return 0u;
    }
}

void proc_core_init(void);
void proc_core_register_task(u32 task_id, u32 parent_task);
void proc_core_set_parent(u32 task_id, u32 parent_task);
u32 proc_core_parent(u32 task_id);
int proc_core_is_descendant(u32 task_id, u32 ancestor_task);
int proc_core_mark_exit(u32 task_id, i32 exit_code);
int proc_core_wait(u32 task_id, i32* out_exit_code);
int proc_core_wait_any(u32 parent_task, u32* out_child_task, i32* out_exit_code);
int proc_core_has_children(u32 parent_task);
int proc_core_signal_send(u32 task_id, proc_signal_t sig);
int proc_core_signal_poll(u32 task_id, proc_signal_t* out_sig);
int proc_core_status(u32 task_id, proc_status_t* out);

#endif
