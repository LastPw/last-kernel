#include "ktrace.h"
#include "timer.h"

static ktrace_event_t g_ring[KTRACE_RING_SIZE];
static u32 g_head = 0u;
static u32 g_count = 0u;

void ktrace_init(void) {
    g_head = 0u;
    g_count = 0u;
    for (u32 i = 0; i < KTRACE_RING_SIZE; i++) {
        g_ring[i].tick = 0u;
        g_ring[i].type = KTRACE_EVT_NONE;
        g_ring[i].cpu = 0u;
        g_ring[i].a0 = 0u;
        g_ring[i].a1 = 0u;
        g_ring[i].a2 = 0u;
    }
}

void ktrace_push(u16 type, u16 cpu, u32 a0, u32 a1, u32 a2) {
    g_ring[g_head].tick = timer_ticks();
    g_ring[g_head].type = type;
    g_ring[g_head].cpu = cpu;
    g_ring[g_head].a0 = a0;
    g_ring[g_head].a1 = a1;
    g_ring[g_head].a2 = a2;

    g_head = (g_head + 1u) % KTRACE_RING_SIZE;
    if (g_count < KTRACE_RING_SIZE) g_count++;
}

u32 ktrace_count(void) {
    return g_count;
}

int ktrace_get(u32 reverse_index, ktrace_event_t* out) {
    if (!out) return -1;
    if (reverse_index >= g_count) return -1;

    u32 latest = (g_head == 0u) ? (KTRACE_RING_SIZE - 1u) : (g_head - 1u);
    u32 idx = (latest + KTRACE_RING_SIZE - reverse_index) % KTRACE_RING_SIZE;
    *out = g_ring[idx];
    return 0;
}

