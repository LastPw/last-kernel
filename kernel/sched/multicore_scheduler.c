#include "multicore_scheduler.h"

static u32 g_core_count = 1;
static u32 g_pick_cursor = 0;
static u32 g_busy_ticks[MCS_MAX_CORES] __attribute__((aligned(64)));
static u32 g_total_ticks[MCS_MAX_CORES] __attribute__((aligned(64)));

void mcs_init(u32 core_count) {
    if (core_count == 0) {
        core_count = 1;
    }
    if (core_count > MCS_MAX_CORES) {
        core_count = MCS_MAX_CORES;
    }

    g_core_count = core_count;
    g_pick_cursor = 0;

    for (u32 i = 0; i < MCS_MAX_CORES; i++) {
        g_busy_ticks[i] = 0;
        g_total_ticks[i] = 0;
    }
}

u32 mcs_core_count(void) {
    return g_core_count;
}

u32 mcs_pick_core(void) {
    if (g_core_count <= 1) {
        return 0;
    }

    g_pick_cursor = (g_pick_cursor + 1) % g_core_count;
    return g_pick_cursor;
}

void mcs_account_tick(u32 core, int busy) {
    if (core >= g_core_count) {
        core = 0;
    }

    g_total_ticks[core]++;
    if (busy) {
        g_busy_ticks[core]++;
    }
}

void mcs_snapshot(u32* busy_ticks, u32* total_ticks, u32 max_cores) {
    if (!busy_ticks || !total_ticks) {
        return;
    }

    for (u32 i = 0; i < max_cores; i++) {
        if (i < g_core_count) {
            busy_ticks[i] = g_busy_ticks[i];
            total_ticks[i] = g_total_ticks[i];
        } else {
            busy_ticks[i] = 0;
            total_ticks[i] = 0;
        }
    }
}
