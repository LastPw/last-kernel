#include "namespace.h"

#define NS_BIND_MAX 32

typedef struct {
    u32 task_id;
    u32 ns_id;
    u8 used;
} ns_bind_t;

static namespace_info_t ns_table[NS_MAX];
static ns_bind_t binds[NS_BIND_MAX];
static u32 next_id = 1;

static int ns_index(u32 id) {
    for (u32 i = 0; i < NS_MAX; i++) {
        if (ns_table[i].active && ns_table[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

void namespace_init(void) {
    next_id = 1;
    for (u32 i = 0; i < NS_MAX; i++) {
        ns_table[i].active = 0;
    }
    for (u32 i = 0; i < NS_BIND_MAX; i++) {
        binds[i].used = 0;
    }
    (void)namespace_create();
}

int namespace_create(void) {
    for (u32 i = 0; i < NS_MAX; i++) {
        if (!ns_table[i].active) {
            u32 id = next_id++;
            ns_table[i].active = 1;
            ns_table[i].id = id;
            ns_table[i].pid_ns = id;
            ns_table[i].net_ns = id;
            ns_table[i].mount_ns = id;
            ns_table[i].ipc_ns = id;
            return (int)id;
        }
    }
    return -1;
}

int namespace_bind_task(u32 task_id, u32 ns_id) {
    if (ns_index(ns_id) < 0) {
        return -1;
    }
    for (u32 i = 0; i < NS_BIND_MAX; i++) {
        if (binds[i].used && binds[i].task_id == task_id) {
            binds[i].ns_id = ns_id;
            return 0;
        }
    }
    for (u32 i = 0; i < NS_BIND_MAX; i++) {
        if (!binds[i].used) {
            binds[i].used = 1;
            binds[i].task_id = task_id;
            binds[i].ns_id = ns_id;
            return 0;
        }
    }
    return -1;
}

u32 namespace_task_ns(u32 task_id) {
    for (u32 i = 0; i < NS_BIND_MAX; i++) {
        if (binds[i].used && binds[i].task_id == task_id) {
            return binds[i].ns_id;
        }
    }
    return 1;
}

u32 namespace_count(void) {
    u32 n = 0;
    for (u32 i = 0; i < NS_MAX; i++) {
        if (ns_table[i].active) {
            n++;
        }
    }
    return n;
}

int namespace_get_by_index(u32 idx, namespace_info_t* out) {
    if (!out) {
        return -1;
    }
    u32 seen = 0;
    for (u32 i = 0; i < NS_MAX; i++) {
        if (!ns_table[i].active) {
            continue;
        }
        if (seen == idx) {
            *out = ns_table[i];
            return 0;
        }
        seen++;
    }
    return -1;
}
