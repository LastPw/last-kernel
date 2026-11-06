#ifndef WAIT_H
#define WAIT_H

#include "types.h"

#define WAIT_QUEUE_MAX 32u

typedef struct {
    u32 waiters[WAIT_QUEUE_MAX];
    u32 count;
} wait_queue_t;

void wait_queue_init(wait_queue_t* q);
int wait_queue_add_current(wait_queue_t* q);
int wait_queue_remove(wait_queue_t* q, u32 task_id);
int wait_queue_wake_one(wait_queue_t* q);
u32 wait_queue_wake_all(wait_queue_t* q);
u32 wait_queue_count(const wait_queue_t* q);

#endif
