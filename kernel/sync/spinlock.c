#include "spinlock.h"
#include "interrupts.h"

static inline u32 xchg_u32(volatile u32* p, u32 v) {
    __asm__ __volatile__("xchgl %0, %1" : "+r"(v), "+m"(*p) :: "memory");
    return v;
}

static inline u32 irq_flags_read(void) {
    unsigned long flags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    return (u32)flags;
}

static inline int irq_enabled(u32 flags) {
    return (flags & (1u << 9)) != 0u;
}

void spinlock_init(spinlock_t* l) {
    if (!l) return;
    l->lock = 0u;
}

void spinlock_lock(spinlock_t* l) {
    if (!l) return;
    while (xchg_u32(&l->lock, 1u) != 0u) {
        __asm__ __volatile__("pause");
    }
}

void spinlock_unlock(spinlock_t* l) {
    if (!l) return;
    __asm__ __volatile__("" ::: "memory");
    l->lock = 0u;
}

u32 spinlock_lock_irqsave(spinlock_t* l) {
    u32 flags = irq_flags_read();
    interrupts_disable();
    spinlock_lock(l);
    return flags;
}

void spinlock_unlock_irqrestore(spinlock_t* l, u32 flags) {
    spinlock_unlock(l);
    if (irq_enabled(flags)) {
        interrupts_enable();
    }
}
