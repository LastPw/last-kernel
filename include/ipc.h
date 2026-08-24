#ifndef IPC_H
#define IPC_H

#include "types.h"

#define IPC_MAX_TASKS 512
#define IPC_QUEUE_CAP 16
#define IPC_MSG_MAX 64
#define IPC_RATE_WINDOW_TICKS 64u
#define IPC_RATE_LIMIT_PER_WINDOW 24u
#define IPC_MSG_TTL_TICKS 512u

#define IPC_FLAG_NONE      0u
#define IPC_FLAG_CROSS_NS  0x1u
#define IPC_FLAG_KERNEL    0x2u

typedef struct {
    u32 src_task;
    u32 dst_task;
    u32 len;
    u32 flags;
    u32 tick;
    char data[IPC_MSG_MAX];
} ipc_message_t;

typedef struct {
    u32 sent;
    u32 received;
    u32 dropped;
    u32 queues_active;
    u32 max_queue_depth;
    u32 rejected_invalid;
    u32 rejected_dead_src;
    u32 rejected_dead_dst;
    u32 rejected_namespace;
    u32 rejected_rate;
    u32 expired_dropped;
} ipc_stats_t;

void ipc_init(void);
int ipc_send(u32 src_task, u32 dst_task, const char* data, u32 len);
int ipc_send_ex(u32 src_task, u32 dst_task, const char* data, u32 len, u32 flags);
int ipc_recv(u32 dst_task, ipc_message_t* out);
void ipc_stats(ipc_stats_t* out);
void ipc_reset(void);
u8 ipc_hardening_enabled(void);

#endif
