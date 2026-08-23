#include "cgroup.h"
#include "string.h"

#define TASK_BIND_MAX 32

typedef struct {
    u32 task_id;
    u32 cgroup_id;
    u8 used;
} task_bind_t;

static cgroup_info_t groups[CGROUP_MAX];
static task_bind_t bindings[TASK_BIND_MAX];
static u32 next_id = 1;

static int group_index(u32 id) {
    for (u32 i = 0; i < CGROUP_MAX; i++) {
        if (groups[i].active && groups[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static int bind_index(u32 task_id) {
    for (u32 i = 0; i < TASK_BIND_MAX; i++) {
        if (bindings[i].used && bindings[i].task_id == task_id) {
            return (int)i;
        }
    }
    return -1;
}

void cgroup_init(void) {
    next_id = 1;
    for (u32 i = 0; i < CGROUP_MAX; i++) {
        groups[i].active = 0;
    }
    for (u32 i = 0; i < TASK_BIND_MAX; i++) {
        bindings[i].used = 0;
    }

    int root = cgroup_create("root");
    if (root >= 0) {
        cgroup_set_limits((u32)root, 100, 0, 0);
    }
}

int cgroup_create(const char* name) {
    for (u32 i = 0; i < CGROUP_MAX; i++) {
        if (!groups[i].active) {
            groups[i].active = 1;
            groups[i].id = next_id++;
            str_ncpy(groups[i].name, name ? name : "group", CGROUP_NAME_MAX);
            groups[i].cpu_limit_percent = 100;
            groups[i].mem_limit_kb = 0;
            groups[i].io_limit_ops = 0;
            groups[i].cpu_window_ticks = 0;
            groups[i].cpu_used_ticks = 0;
            return (int)groups[i].id;
        }
    }
    return -1;
}

int cgroup_set_limits(u32 id, u32 cpu_percent, u32 mem_kb, u32 io_ops) {
    int idx = group_index(id);
    if (idx < 0) {
        return -1;
    }
    if (cpu_percent == 0 || cpu_percent > 100) {
        cpu_percent = 100;
    }
    groups[idx].cpu_limit_percent = cpu_percent;
    groups[idx].mem_limit_kb = mem_kb;
    groups[idx].io_limit_ops = io_ops;
    return 0;
}

int cgroup_bind_task(u32 task_id, u32 cgroup_id) {
    if (group_index(cgroup_id) < 0) {
        return -1;
    }
    int bi = bind_index(task_id);
    if (bi >= 0) {
        bindings[bi].cgroup_id = cgroup_id;
        return 0;
    }
    for (u32 i = 0; i < TASK_BIND_MAX; i++) {
        if (!bindings[i].used) {
            bindings[i].used = 1;
            bindings[i].task_id = task_id;
            bindings[i].cgroup_id = cgroup_id;
            return 0;
        }
    }
    return -1;
}

u32 cgroup_task_group(u32 task_id) {
    int bi = bind_index(task_id);
    if (bi >= 0) {
        return bindings[bi].cgroup_id;
    }
    return 1;
}

int cgroup_cpu_allow(u32 task_id, u32 now_tick) {
    u32 gid = cgroup_task_group(task_id);
    int gi = group_index(gid);
    if (gi < 0) {
        return 1;
    }

    cgroup_info_t* g = &groups[gi];
    if (g->cpu_window_ticks == 0 || (now_tick - g->cpu_window_ticks) >= 100) {
        g->cpu_window_ticks = now_tick;
        g->cpu_used_ticks = 0;
    }

    u32 limit = g->cpu_limit_percent;
    u32 max_ticks = limit;
    if (g->cpu_used_ticks >= max_ticks) {
        return 0;
    }
    g->cpu_used_ticks++;
    return 1;
}

int cgroup_mem_allow(u32 task_id, u32 new_total_bytes) {
    u32 gid = cgroup_task_group(task_id);
    int gi = group_index(gid);
    if (gi < 0) {
        return 1;
    }
    cgroup_info_t* g = &groups[gi];
    if (g->mem_limit_kb == 0) {
        return 1;
    }
    return (new_total_bytes <= (g->mem_limit_kb * 1024u)) ? 1 : 0;
}

u32 cgroup_count(void) {
    u32 n = 0;
    for (u32 i = 0; i < CGROUP_MAX; i++) {
        if (groups[i].active) {
            n++;
        }
    }
    return n;
}

int cgroup_get_by_index(u32 idx, cgroup_info_t* out) {
    if (!out) {
        return -1;
    }
    u32 seen = 0;
    for (u32 i = 0; i < CGROUP_MAX; i++) {
        if (!groups[i].active) {
            continue;
        }
        if (seen == idx) {
            *out = groups[i];
            return 0;
        }
        seen++;
    }
    return -1;
}
