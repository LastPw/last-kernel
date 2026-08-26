/* Application-processor worker cores.
 *
 * Each AP that finishes the SMP trampoline enters ap_worker_entry():
 *   1. Loads the shared GDT and IDT descriptors (the trampoline's
 *      temporary GDT uses the same selector layout, so CS/SS stay valid).
 *   2. Enables interrupts (the shared IDT routes spurious vectors safely).
 *   3. Enters a spin-poll loop watching a per-core work slot.
 *
 * Work submission is a synchronous handoff: the caller stores fn+arg,
 * sets pending, spins on done.  x86 TSO ordering plus compiler barriers
 * make this correct for one-writer/one-reader per slot.
 *
 * Workers must never touch the scheduler's `current` pointer or any
 * other single-CPU state -- they are pure compute offload targets. */

#include "ap_worker.h"
#include "hw_topology.h"
#include "serial.h"
#include "string.h"

/* ── GDT / IDT descriptor exposure ────────────────────────────────────── */

extern void* gdt_descriptor(void);
extern void* idt_descriptor(void);

static inline void lgdt_load(u64 desc_addr) {
    __asm__ __volatile__("lgdt (%0)" : : "r"(desc_addr) : "memory");
}

static inline void lidt_load(u64 desc_addr) {
    __asm__ __volatile__("lidt (%0)" : : "r"(desc_addr) : "memory");
}

static inline void reload_segments(void) {
    /* Reload CS via far-return; reload data segment registers. */
    __asm__ __volatile__(
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movq $0x10, %%rax\n"
        "movq %%rax, %%ds\n"
        "movq %%rax, %%es\n"
        "movq %%rax, %%ss\n"
        "xorq %%rax, %%rax\n"
        "movq %%rax, %%fs\n"
        "movq %%rax, %%gs\n"
        : : : "rax", "memory");
}

/* ── work slots ───────────────────────────────────────────────────────── */

#define APW_MAX_CORES 32u

typedef struct {
    ap_work_fn fn;
    void* arg;
    volatile u8 pending;
    volatile u8 done;
} ap_work_slot_t;

static ap_work_slot_t g_slots[APW_MAX_CORES];
static volatile u32 g_tick_count[APW_MAX_CORES];
static u8 g_worker_online[APW_MAX_CORES];

int ap_worker_submit(u32 hw_index, ap_work_fn fn, void* arg) {
    if (hw_index >= APW_MAX_CORES || !g_worker_online[hw_index] || !fn)
        return -1;

    ap_work_slot_t* slot = &g_slots[hw_index];
    if (slot->pending) return -2; /* busy */

    slot->fn = fn;
    slot->arg = arg;
    __asm__ __volatile__("" : : : "memory"); /* compiler barrier */
    slot->done = 0u;
    slot->pending = 1u;

    /* wait for completion */
    while (!slot->done) {
        __asm__ __volatile__("pause");
    }
    return 0;
}

u32 ap_worker_tick_count(u32 hw_index) {
    return hw_index < APW_MAX_CORES ? g_tick_count[hw_index] : 0u;
}

u32 ap_worker_online_mask(void) {
    u32 mask = 0u;
    for (u32 i = 0; i < APW_MAX_CORES && i < HW_TOPO_MAX_CORES; i++) {
        if (g_worker_online[i]) mask |= (1u << i);
    }
    return mask;
}

u32 ap_worker_core_count(void) {
    u32 n = 0u;
    for (u32 i = 0; i < APW_MAX_CORES; i++) {
        if (g_worker_online[i]) n++;
    }
    return n;
}

/* ── AP entrypoint ────────────────────────────────────────────────────── */

void ap_worker_entry(u32 hw_index) {
    if (hw_index >= APW_MAX_CORES) {
        for (;;) __asm__ __volatile__("hlt");
    }

    /* Load the shared descriptor tables so interrupts and future ring
       transitions use the kernel's real GDT/IDT rather than the
       trampoline's stripped-down copy. */
    void* gd = gdt_descriptor();
    void* id = idt_descriptor();
    if (gd) lgdt_load((u64)(uintptr_t)gd);
    if (id) lidt_load((u64)(uintptr_t)id);
    reload_segments();

    memset(&g_slots[hw_index], 0, sizeof(ap_work_slot_t));
    g_worker_online[hw_index] = 1u;

    serial_write("[APW] core ");
    serial_write_dec(hw_index);
    serial_writeln(" worker online");

    u32 poll_counter = 0u;

    for (;;) {
        ap_work_slot_t* slot = &g_slots[hw_index];

        if (slot->pending) {
            __asm__ __volatile__("" : : : "memory"); /* read barrier */
            slot->fn(slot->arg);
            __asm__ __volatile__("" : : : "memory");
            slot->done = 1u;
            slot->pending = 0u;
        } else {
            __asm__ __volatile__("pause");
        }

        /* Liveness heartbeat every ~1M polls */
        poll_counter++;
        if ((poll_counter & 0xFFFFFu) == 0u) {
            g_tick_count[hw_index]++;
        }
    }
}
