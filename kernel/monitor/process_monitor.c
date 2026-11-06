#include "process_monitor.h"
#include "threat_logger.h"
#include "string.h"

static process_info_t table[PROCESS_MON_MAX] __attribute__((aligned(64)));
static u32 registered = 0;

static int idx_for_task(u32 task_id) {
    for (u32 i = 0; i < PROCESS_MON_MAX; i++) {
        if (table[i].task_id == task_id) {
            return (int)i;
        }
    }
    return -1;
}

void process_monitor_init(void) {
    registered = 0;
    for (u32 i = 0; i < PROCESS_MON_MAX; i++) {
        table[i].task_id = 0xFFFFFFFFu;
        table[i].name[0] = '\0';
        table[i].runtime_ticks = 0;
        table[i].suspicious_score = 0;
        table[i].blocked = 0;
    }
}

void process_monitor_register(u32 task_id) {
    process_monitor_register_named(task_id, "task");
}

void process_monitor_register_named(u32 task_id, const char* name) {
    if (idx_for_task(task_id) >= 0) {
        return;
    }

    for (u32 i = 0; i < PROCESS_MON_MAX; i++) {
        if (table[i].task_id == 0xFFFFFFFFu) {
            table[i].task_id = task_id;
            str_ncpy(table[i].name, name ? name : "task", PROCESS_NAME_MAX);
            table[i].runtime_ticks = 0;
            table[i].suspicious_score = 0;
            table[i].blocked = 0;
            registered++;
            return;
        }
    }
}

void process_monitor_unregister(u32 task_id) {
    int idx = idx_for_task(task_id);
    if (idx < 0) return;
    table[(u32)idx].task_id = 0xFFFFFFFFu;
    table[(u32)idx].name[0] = '\0';
    table[(u32)idx].runtime_ticks = 0u;
    table[(u32)idx].suspicious_score = 0u;
    table[(u32)idx].blocked = 0u;
    if (registered > 0u) registered--;
}

void process_monitor_on_tick(u32 task_id, int busy) {
    int idx = idx_for_task(task_id);
    if (idx < 0) {
        return;
    }

    process_info_t* p = &table[idx];
    p->runtime_ticks++;

    if (busy) {
        if ((p->runtime_ticks % 400) == 0) {
            p->suspicious_score += 5;
        }
    } else if (p->suspicious_score > 0) {
        p->suspicious_score--;
    }

    if (p->suspicious_score > 30) {
        p->blocked = 1;
        threat_logger_log(THREAT_CPU_ABUSE, task_id, p->suspicious_score, p->runtime_ticks);
    }
}

int process_monitor_is_blocked(u32 task_id) {
    int idx = idx_for_task(task_id);
    if (idx < 0) {
        return 0;
    }
    return table[idx].blocked ? 1 : 0;
}

int process_monitor_snapshot(u32 task_id, process_info_t* out) {
    int idx = idx_for_task(task_id);
    if (idx < 0 || !out) {
        return -1;
    }

    *out = table[idx];
    return 0;
}

int process_monitor_snapshot_at(u32 index, process_info_t* out) {
    u32 seen = 0u;
    if (!out) return -1;
    for (u32 i = 0u; i < PROCESS_MON_MAX; i++) {
        if (table[i].task_id == 0xFFFFFFFFu) continue;
        if (seen++ == index) {
            *out = table[i];
            return 0;
        }
    }
    return -1;
}

u32 process_monitor_task_count(void) {
    return registered;
}

u32 process_monitor_blocked_count(void) {
    u32 n = 0;
    for (u32 i = 0; i < PROCESS_MON_MAX; i++) {
        if (table[i].task_id != 0xFFFFFFFFu && table[i].blocked) {
            n++;
        }
    }
    return n;
}
