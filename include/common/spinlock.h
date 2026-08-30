#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "types.h"

typedef struct {
    volatile u32 lock;
} spinlock_t;

void spinlock_init(spinlock_t* l);
void spinlock_lock(spinlock_t* l);
void spinlock_unlock(spinlock_t* l);
u32 spinlock_lock_irqsave(spinlock_t* l);
void spinlock_unlock_irqrestore(spinlock_t* l, u32 flags);

#endif
