#ifndef PANICLOG_H
#define PANICLOG_H

#include "types.h"

#define PANICLOG_CAP 32
#define PANICLOG_MSG_MAX 64

typedef struct {
    u32 id;
    u32 task_id;
    u32 tick;
    u32 int_no;
    u32 err_code;
    u32 eip;
    u32 addr_hint;
    u32 heap_corruptions;
    u32 heap_fragmentation;
    u32 frames_used;
    u32 frames_total;
    u32 watchdog_stalls;
    u32 watchdog_actions;
    u32 msg_hash;
    char msg[PANICLOG_MSG_MAX];
} panic_record_t;

void paniclog_init(void);
void paniclog_push(const char* msg,
                   u32 int_no,
                   u32 err_code,
                   u32 eip,
                   u32 addr_hint,
                   u32 tick,
                   u32 heap_corruptions,
                   u32 heap_fragmentation,
                   u32 frames_used,
                   u32 frames_total,
                   u32 watchdog_stalls,
                   u32 watchdog_actions);
void paniclog_push_for_task(u32 task_id, const char* msg,
                            u32 int_no, u32 err_code, u32 eip, u32 addr_hint,
                            u32 tick, u32 heap_corruptions, u32 heap_fragmentation,
                            u32 frames_used, u32 frames_total,
                            u32 watchdog_stalls, u32 watchdog_actions);
void paniclog_load_persistent(void);
void paniclog_persist_last(void);
u32 paniclog_count(void);
int paniclog_get(u32 reverse_index, panic_record_t* out);
void paniclog_clear(void);

#endif
