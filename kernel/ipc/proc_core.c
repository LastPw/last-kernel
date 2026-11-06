#include "proc_core.h"

typedef struct {
    u8 used;
    u8 alive;
    u8 zombie;
    u8 qhead;
    u8 qtail;
    u8 qcount;
    u32 task_id;
    u32 parent_task;
    i32 exit_code;
    u32 last_signal;
    u32 waiters;
    proc_signal_t q[8];
    /* User-space signal handlers: indexed by signal number (1=SIGKILL...). */
    u32 sig_handler[SIG_HANDLER_MAX];
} proc_entry_t;

static proc_entry_t g_p[PROC_MAX_TASKS];

static int slot_of(u32 task_id) {
    for (u32 i = 0u; i < PROC_MAX_TASKS; i++) {
        if (g_p[i].used && g_p[i].task_id == task_id) return (int)i;
    }
    return -1;
}

static int slot_alloc(u32 task_id) {
    for (u32 i = 0u; i < PROC_MAX_TASKS; i++) {
        if (!g_p[i].used) {
            g_p[i].used = 1u;
            g_p[i].alive = 1u;
            g_p[i].zombie = 0u;
            g_p[i].qhead = 0u;
            g_p[i].qtail = 0u;
            g_p[i].qcount = 0u;
            g_p[i].task_id = task_id;
            g_p[i].parent_task = 0u;
            g_p[i].exit_code = 0;
            g_p[i].last_signal = 0u;
            g_p[i].waiters = 0u;
            for (u32 si = 0u; si < SIG_HANDLER_MAX; si++) g_p[i].sig_handler[si] = 0u;
            return (int)i;
        }
    }
    return -1;
}

/* ── Signal handler registry ─────────────────────────────────────────── */

int proc_core_sigaction(u32 task_id, u32 sig, u32 handler_addr) {
    int s = slot_of(task_id);
    if (s < 0 || sig == 0u || sig > SIG_HANDLER_MAX) return -1;
    g_p[s].sig_handler[sig - 1u] = handler_addr;
    return 0;
}

u32 proc_core_sigaction_get(u32 task_id, u32 sig) {
    int s = slot_of(task_id);
    if (s < 0 || sig == 0u || sig > SIG_HANDLER_MAX) return 0u;
    return g_p[s].sig_handler[sig - 1u];
}

void proc_core_init(void) {
    for (u32 i = 0u; i < PROC_MAX_TASKS; i++) {
        g_p[i].used = 0u;
        g_p[i].alive = 0u;
        g_p[i].zombie = 0u;
        g_p[i].qhead = 0u;
        g_p[i].qtail = 0u;
        g_p[i].qcount = 0u;
        g_p[i].task_id = 0u;
        g_p[i].parent_task = 0u;
        g_p[i].exit_code = 0;
        g_p[i].last_signal = 0u;
        g_p[i].waiters = 0u;
    }
}

void proc_core_register_task(u32 task_id, u32 parent_task) {
    int s = slot_of(task_id);
    if (s < 0) s = slot_alloc(task_id);
    if (s < 0) return;
    g_p[s].alive = 1u;
    g_p[s].zombie = 0u;
    g_p[s].parent_task = parent_task;
}

void proc_core_set_parent(u32 task_id, u32 parent_task) {
    int s = slot_of(task_id);
    if (s < 0) return;
    g_p[s].parent_task = parent_task;
}

u32 proc_core_parent(u32 task_id) {
    int s = slot_of(task_id);
    if (s < 0) return 0u;
    return g_p[s].parent_task;
}

int proc_core_is_descendant(u32 task_id, u32 ancestor_task) {
    u32 current = task_id;
    if (current == 0u || ancestor_task == 0u || current == ancestor_task) return 0;
    for (u32 depth = 0u; depth < PROC_MAX_TASKS; depth++) {
        current = proc_core_parent(current);
        if (current == ancestor_task) return 1;
        if (current == 0u) return 0;
    }
    return 0;
}

int proc_core_mark_exit(u32 task_id, i32 exit_code) {
    int s = slot_of(task_id);
    if (s < 0) return -1;
    g_p[s].alive = 0u;
    g_p[s].zombie = 1u;
    g_p[s].exit_code = exit_code;
    return 0;
}

int proc_core_wait(u32 task_id, i32* out_exit_code) {
    int s = slot_of(task_id);
    if (s < 0 || !out_exit_code) return -1;
    g_p[s].waiters++;
    if (g_p[s].zombie) {
        *out_exit_code = g_p[s].exit_code;
        g_p[s].waiters--;
        /* A successful wait reaps the zombie and releases its process slot. */
        g_p[s].used = 0u;
        g_p[s].alive = 0u;
        g_p[s].zombie = 0u;
        return 0;
    }
    g_p[s].waiters--;
    return -2;
}

int proc_core_has_children(u32 parent_task) {
    if (parent_task == 0u) return 0;
    for (u32 i = 0u; i < PROC_MAX_TASKS; i++) {
        if (g_p[i].used && g_p[i].parent_task == parent_task && (g_p[i].alive || g_p[i].zombie)) {
            return 1;
        }
    }
    return 0;
}

int proc_core_wait_any(u32 parent_task, u32* out_child_task, i32* out_exit_code) {
    if (parent_task == 0u || !out_child_task || !out_exit_code) return -1;
    int has_child = 0;

    for (u32 i = 0u; i < PROC_MAX_TASKS; i++) {
        if (g_p[i].used && g_p[i].parent_task == parent_task) {
            has_child = 1;
            if (g_p[i].zombie) {
                *out_child_task = g_p[i].task_id;
                *out_exit_code = g_p[i].exit_code;
                g_p[i].used = 0u;
                g_p[i].alive = 0u;
                g_p[i].zombie = 0u;
                return 0;
            }
        }
    }
    if (!has_child) return -1;
    return -2;
}

int proc_core_signal_send(u32 task_id, proc_signal_t sig) {
    int s = slot_of(task_id);
    if (s < 0 || sig == PROC_SIG_NONE) return -1;
    if (g_p[s].qcount >= 8u) return -2;
    g_p[s].q[g_p[s].qtail] = sig;
    g_p[s].qtail = (u8)((g_p[s].qtail + 1u) % 8u);
    g_p[s].qcount++;
    g_p[s].last_signal = (u32)sig;
    return 0;
}

int proc_core_signal_poll(u32 task_id, proc_signal_t* out_sig) {
    int s = slot_of(task_id);
    if (s < 0 || !out_sig) return -1;
    if (g_p[s].qcount == 0u) return -2;
    *out_sig = g_p[s].q[g_p[s].qhead];
    g_p[s].qhead = (u8)((g_p[s].qhead + 1u) % 8u);
    g_p[s].qcount--;
    return 0;
}

int proc_core_status(u32 task_id, proc_status_t* out) {
    int s = slot_of(task_id);
    if (s < 0 || !out) return -1;
    out->task_id = g_p[s].task_id;
    out->parent_task = g_p[s].parent_task;
    out->alive = g_p[s].alive;
    out->zombie = g_p[s].zombie;
    out->pending_signals = g_p[s].qcount;
    out->exit_code = g_p[s].exit_code;
    out->last_signal = g_p[s].last_signal;
    out->waiters = g_p[s].waiters;
    return 0;
}
