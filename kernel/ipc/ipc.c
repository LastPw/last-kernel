#include "ipc.h"
#include "audit.h"
#include "kshield.h"
#include "namespace.h"
#include "proc_core.h"
#include "rust_bridge.h"
#include "timer.h"
#include "wait.h"

typedef struct {
    u8 used;
    u8 head;
    u8 tail;
    u8 count;
    ipc_message_t q[IPC_QUEUE_CAP];
    wait_queue_t waiters;
} ipc_queue_t;

typedef struct {
    u32 window_start_tick;
    u32 sent_in_window;
} ipc_rate_t;

static ipc_queue_t g_q[IPC_MAX_TASKS];
static ipc_rate_t g_rate[IPC_MAX_TASKS];
static ipc_stats_t g_stats;
static u8 g_hardening = 1u;

static void copy_payload(char* dst, const char* src, u32 n) {
    if (!dst || !src) return;
    for (u32 i = 0u; i < n; i++) {
        dst[i] = src[i];
    }
    if (n < IPC_MSG_MAX) dst[n] = '\0';
    else dst[IPC_MSG_MAX - 1u] = '\0';
}

static u32 tick_diff(u32 now, u32 then) {
    return now - then;
}

static u8 msg_expired(const ipc_message_t* m, u32 now) {
    if (!m) return 1u;
    return tick_diff(now, m->tick) > IPC_MSG_TTL_TICKS ? 1u : 0u;
}

static void queue_activate(ipc_queue_t* q) {
    if (!q) return;
    if (!q->used) {
        q->used = 1u;
        g_stats.queues_active++;
    }
}

static void queue_deactivate_if_empty(ipc_queue_t* q) {
    if (!q) return;
    if (q->used && q->count == 0u) {
        q->used = 0u;
        q->head = 0u;
        q->tail = 0u;
        if (g_stats.queues_active > 0u) g_stats.queues_active--;
    }
}

static u8 task_alive(u32 task_id) {
    proc_status_t st;
    if (task_id == 0u) return 1u;
    if (proc_core_status(task_id, &st) != 0) return 0u;
    return st.alive ? 1u : 0u;
}

static u8 namespace_allowed(u32 src_task, u32 dst_task, u32 flags) {
    u32 src_ns;
    u32 dst_ns;
    if (!g_hardening) return 1u;
    if (src_task == 0u || dst_task == 0u) return 1u;
    src_ns = namespace_task_ns(src_task);
    dst_ns = namespace_task_ns(dst_task);
    if (src_ns == dst_ns) return 1u;
    if (flags & IPC_FLAG_CROSS_NS) {
        if (kshield_lockdown() == 0u) return 1u;
    }
    return 0u;
}

static u8 rate_allow(u32 src_task, u32 now_tick) {
    ipc_rate_t* r;
    if (src_task >= IPC_MAX_TASKS) return 0u;
    if (!g_hardening) return 1u;

    r = &g_rate[src_task];
    if (r->window_start_tick == 0u) {
        r->window_start_tick = now_tick;
        r->sent_in_window = 0u;
    }
    if (tick_diff(now_tick, r->window_start_tick) > IPC_RATE_WINDOW_TICKS) {
        r->window_start_tick = now_tick;
        r->sent_in_window = 0u;
    }
    if (r->sent_in_window >= IPC_RATE_LIMIT_PER_WINDOW) {
        return 0u;
    }
    r->sent_in_window++;
    return 1u;
}

static void queue_prune_expired(ipc_queue_t* q, u32 now_tick) {
    while (q && q->used && q->count > 0u) {
        ipc_message_t* m = &q->q[q->head];
        if (!msg_expired(m, now_tick)) break;
        q->head = (u8)((q->head + 1u) % IPC_QUEUE_CAP);
        q->count--;
        g_stats.dropped++;
        g_stats.expired_dropped++;
    }
    queue_deactivate_if_empty(q);
}

void ipc_reset(void) {
    for (u32 i = 0u; i < IPC_MAX_TASKS; i++) {
        g_q[i].used = 0u;
        g_q[i].head = 0u;
        g_q[i].tail = 0u;
        g_q[i].count = 0u;
        wait_queue_init(&g_q[i].waiters);
        g_rate[i].window_start_tick = 0u;
        g_rate[i].sent_in_window = 0u;
    }
    g_stats.sent = 0u;
    g_stats.received = 0u;
    g_stats.dropped = 0u;
    g_stats.queues_active = 0u;
    g_stats.max_queue_depth = 0u;
    g_stats.rejected_invalid = 0u;
    g_stats.rejected_dead_src = 0u;
    g_stats.rejected_dead_dst = 0u;
    g_stats.rejected_namespace = 0u;
    g_stats.rejected_rate = 0u;
    g_stats.expired_dropped = 0u;
}

void ipc_init(void) {
    ipc_reset();
}

u8 ipc_hardening_enabled(void) {
    return g_hardening;
}

int ipc_send(u32 src_task, u32 dst_task, const char* data, u32 len) {
    return ipc_send_ex(src_task, dst_task, data, len, IPC_FLAG_NONE);
}

int ipc_send_ex(u32 src_task, u32 dst_task, const char* data, u32 len, u32 flags) {
    ipc_queue_t* q;
    ipc_message_t* m;
    u32 now_tick = timer_ticks();
    u32 src_ns = 1u;
    u32 dst_ns = 1u;
    u32 same_ns = 1u;
    u32 policy_action = 0u;
    u32 policy_score = 0u;

    if (!data || len == 0u || src_task >= IPC_MAX_TASKS || dst_task >= IPC_MAX_TASKS) {
        g_stats.rejected_invalid++;
        return -1;
    }
    if (!task_alive(src_task)) {
        g_stats.rejected_dead_src++;
        audit_log(AUDIT_EVT_TASK_VIOLATION, src_task, 0x49504301u);
        return -3;
    }
    if (!task_alive(dst_task)) {
        g_stats.rejected_dead_dst++;
        audit_log(AUDIT_EVT_TASK_VIOLATION, dst_task, 0x49504302u);
        return -4;
    }
    if (!namespace_allowed(src_task, dst_task, flags)) {
        g_stats.rejected_namespace++;
        audit_log(AUDIT_EVT_TASK_VIOLATION, src_task, 0x49504303u);
        return -5;
    }
    if (!rate_allow(src_task, now_tick)) {
        g_stats.rejected_rate++;
        audit_log(AUDIT_EVT_TASK_VIOLATION, src_task, 0x49504304u);
        return -6;
    }

    if (src_task != 0u && dst_task != 0u) {
        src_ns = namespace_task_ns(src_task);
        dst_ns = namespace_task_ns(dst_task);
        same_ns = (src_ns == dst_ns) ? 1u : 0u;
    }

    q = &g_q[dst_task];
    queue_activate(q);
    queue_prune_expired(q, now_tick);

    if (rust_bridge_policy_ipc(
        src_task,
        dst_task,
        same_ns,
        (u32)q->count,
        kshield_lockdown(),
        g_rate[src_task].sent_in_window,
        &policy_action,
        &policy_score) == 0) {
        (void)policy_score;
        if (policy_action >= 2u) {
            g_stats.rejected_rate++;
            audit_log(AUDIT_EVT_TASK_VIOLATION, src_task, 0x49504305u);
            return -6;
        }
        if (policy_action == 1u && q->count >= (IPC_QUEUE_CAP - 2u)) {
            g_stats.rejected_rate++;
            audit_log(AUDIT_EVT_TASK_VIOLATION, src_task, 0x49504306u);
            return -6;
        }
    }

    if (q->count >= IPC_QUEUE_CAP) {
        g_stats.dropped++;
        return -2;
    }

    if (len >= IPC_MSG_MAX) len = IPC_MSG_MAX - 1u;
    m = &q->q[q->tail];
    m->src_task = src_task;
    m->dst_task = dst_task;
    m->len = len;
    m->flags = flags;
    m->tick = now_tick;
    copy_payload(m->data, data, len);

    q->tail = (u8)((q->tail + 1u) % IPC_QUEUE_CAP);
    q->count++;
    if (q->count > g_stats.max_queue_depth) g_stats.max_queue_depth = q->count;
    g_stats.sent++;
    (void)wait_queue_wake_one(&q->waiters);
    return 0;
}

int ipc_recv(u32 dst_task, ipc_message_t* out) {
    ipc_queue_t* q;
    if (dst_task >= IPC_MAX_TASKS || !out) {
        g_stats.rejected_invalid++;
        return -1;
    }
    if (!task_alive(dst_task)) {
        g_stats.rejected_dead_dst++;
        return -3;
    }

    q = &g_q[dst_task];
    queue_prune_expired(q, timer_ticks());
    if (!q->used || q->count == 0u) {
        return -2;
    }

    *out = q->q[q->head];
    q->head = (u8)((q->head + 1u) % IPC_QUEUE_CAP);
    q->count--;
    queue_deactivate_if_empty(q);
    g_stats.received++;
    return 0;
}

void ipc_stats(ipc_stats_t* out) {
    if (!out) return;
    *out = g_stats;
}
