#ifndef NAMESPACE_H
#define NAMESPACE_H

#include "types.h"

#define NS_MAX 8

typedef struct {
    u32 id;
    u32 pid_ns;
    u32 net_ns;
    u32 mount_ns;
    u32 ipc_ns;
    u8 active;
} namespace_info_t;

void namespace_init(void);
int namespace_create(void);
int namespace_bind_task(u32 task_id, u32 ns_id);
u32 namespace_task_ns(u32 task_id);
u32 namespace_count(void);
int namespace_get_by_index(u32 idx, namespace_info_t* out);

#endif
