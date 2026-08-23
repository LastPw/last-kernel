#include "threat_logger.h"
#include "timer.h"
#include "spinlock.h"
#include "kshield.h"

static threat_record_t ring[THREAT_LOG_CAP] __attribute__((aligned(64)));
static u32 head = 0;
static u32 next_id = 1;
static spinlock_t lock;

void threat_logger_init(void) {
    spinlock_init(&lock);
    u32 irqf = spinlock_lock_irqsave(&lock);
    head = 0;
    next_id = 1;
    for (u32 i = 0; i < THREAT_LOG_CAP; i++) {
        ring[i].id = 0;
        ring[i].tick = 0;
        ring[i].task_id = 0;
        ring[i].type = THREAT_NONE;
        ring[i].arg0 = 0;
        ring[i].arg1 = 0;
    }
    spinlock_unlock_irqrestore(&lock, irqf);
}

void threat_logger_log(threat_type_t type, u32 task_id, u32 arg0, u32 arg1) {
    u32 irqf = spinlock_lock_irqsave(&lock);
    u32 idx = head % THREAT_LOG_CAP;
    ring[idx].id = next_id++;
    ring[idx].tick = timer_ticks();
    ring[idx].task_id = task_id;
    ring[idx].type = (u32)type;
    ring[idx].arg0 = arg0;
    ring[idx].arg1 = arg1;
    head++;
    spinlock_unlock_irqrestore(&lock, irqf);
    kshield_on_threat(type, task_id, arg0, arg1);
}

u32 threat_logger_count(void) {
    u32 irqf = spinlock_lock_irqsave(&lock);
    u32 h = head;
    spinlock_unlock_irqrestore(&lock, irqf);
    return h;
}

int threat_logger_get(u32 reverse_index, threat_record_t* out) {
    u32 irqf = spinlock_lock_irqsave(&lock);
    if (!out) {
        spinlock_unlock_irqrestore(&lock, irqf);
        return -1;
    }
    if (head == 0 || reverse_index >= THREAT_LOG_CAP || reverse_index >= head) {
        spinlock_unlock_irqrestore(&lock, irqf);
        return -1;
    }

    u32 pos = (head - 1 - reverse_index) % THREAT_LOG_CAP;
    if (ring[pos].id == 0) {
        spinlock_unlock_irqrestore(&lock, irqf);
        return -1;
    }

    *out = ring[pos];
    spinlock_unlock_irqrestore(&lock, irqf);
    return 0;
}
