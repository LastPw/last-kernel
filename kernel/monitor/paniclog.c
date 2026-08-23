#include "paniclog.h"
#include "spinlock.h"
#include "string.h"
#include "persistent_disk.h"

static panic_record_t ring[PANICLOG_CAP] __attribute__((aligned(64)));
static u32 head = 0u;
static u32 next_id = 1u;
static spinlock_t lock;

static void clear_record(panic_record_t* r) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
}

static u32 hash_msg(const char* s) {
    u32 h = 2166136261u;
    if (!s) return h;
    for (u32 i = 0; s[i]; i++) {
        h ^= (u8)s[i];
        h *= 16777619u;
    }
    return h;
}

void paniclog_init(void) {
    spinlock_init(&lock);
    u32 irqf = spinlock_lock_irqsave(&lock);
    head = 0u;
    next_id = 1u;
    for (u32 i = 0; i < PANICLOG_CAP; i++) {
        clear_record(&ring[i]);
    }
    spinlock_unlock_irqrestore(&lock, irqf);
}

void paniclog_push_for_task(u32 task_id, const char* msg,
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
                   u32 watchdog_actions) {
    u32 irqf = spinlock_lock_irqsave(&lock);

    u32 idx = head % PANICLOG_CAP;
    ring[idx].id = next_id++;
    ring[idx].task_id = task_id;
    ring[idx].tick = tick;
    ring[idx].int_no = int_no;
    ring[idx].err_code = err_code;
    ring[idx].eip = eip;
    ring[idx].addr_hint = addr_hint;
    ring[idx].heap_corruptions = heap_corruptions;
    ring[idx].heap_fragmentation = heap_fragmentation;
    ring[idx].frames_used = frames_used;
    ring[idx].frames_total = frames_total;
    ring[idx].watchdog_stalls = watchdog_stalls;
    ring[idx].watchdog_actions = watchdog_actions;
    ring[idx].msg_hash = hash_msg(msg);

    if (msg && msg[0]) {
        str_ncpy(ring[idx].msg, msg, PANICLOG_MSG_MAX);
    } else {
        ring[idx].msg[0] = '\0';
    }

    head++;
    spinlock_unlock_irqrestore(&lock, irqf);
}

void paniclog_push(const char* msg,
                   u32 int_no, u32 err_code, u32 eip, u32 addr_hint,
                   u32 tick, u32 heap_corruptions, u32 heap_fragmentation,
                   u32 frames_used, u32 frames_total,
                   u32 watchdog_stalls, u32 watchdog_actions) {
    paniclog_push_for_task(0u, msg, int_no, err_code, eip, addr_hint, tick,
                           heap_corruptions, heap_fragmentation, frames_used,
                           frames_total, watchdog_stalls, watchdog_actions);
}

void paniclog_load_persistent(void) {
    panic_record_t record;
    if (!pdisk_ready() || pdisk_load_panic(&record, sizeof(record)) != 0 || record.id == 0u) return;
    u32 irqf = spinlock_lock_irqsave(&lock);
    ring[0] = record;
    head = 1u;
    next_id = record.id + 1u;
    spinlock_unlock_irqrestore(&lock, irqf);
}

void paniclog_persist_last(void) {
    panic_record_t record;
    if (paniclog_get(0u, &record) == 0) (void)pdisk_save_panic(&record, sizeof(record));
}

u32 paniclog_count(void) {
    u32 irqf = spinlock_lock_irqsave(&lock);
    u32 c = head;
    spinlock_unlock_irqrestore(&lock, irqf);
    return c;
}

int paniclog_get(u32 reverse_index, panic_record_t* out) {
    if (!out) return -1;

    u32 irqf = spinlock_lock_irqsave(&lock);
    if (head == 0u || reverse_index >= PANICLOG_CAP || reverse_index >= head) {
        spinlock_unlock_irqrestore(&lock, irqf);
        return -1;
    }

    u32 pos = (head - 1u - reverse_index) % PANICLOG_CAP;
    if (ring[pos].id == 0u) {
        spinlock_unlock_irqrestore(&lock, irqf);
        return -1;
    }

    *out = ring[pos];
    spinlock_unlock_irqrestore(&lock, irqf);
    return 0;
}

void paniclog_clear(void) {
    u32 irqf = spinlock_lock_irqsave(&lock);
    for (u32 i = 0; i < PANICLOG_CAP; i++) {
        clear_record(&ring[i]);
    }
    head = 0u;
    next_id = 1u;
    spinlock_unlock_irqrestore(&lock, irqf);
    (void)pdisk_clear_panic();
}
