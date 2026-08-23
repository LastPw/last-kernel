#include "swap.h"

static u32 g_swap_max_kb = 0;
static u32 g_swap_used_kb = 0;

void swap_init(u32 max_kb) {
    g_swap_max_kb = max_kb;
    g_swap_used_kb = 0;
}

void swap_init_scaled(u32 total_ram_bytes) {
    /* HW-max: swap = 25% of total RAM, capped at 2 GB; the old 64 MB cap
       was a 32-bit-kernel floor.  Minimum 1 MB. */
    u32 kb = total_ram_bytes / 4096u;              /* 25% */
    if (kb < 1024u) kb = 1024u;                    /* floor: 1 MB */
    if (kb > (2u * 1024u * 1024u)) kb = 2u * 1024u * 1024u;  /* cap: 2 GB */
    swap_init(kb);
}

void swap_balance(u32 total_bytes, u32 used_bytes) {
    if (g_swap_max_kb == 0 || total_bytes == 0) {
        return;
    }

    u32 high = (total_bytes * 85u) / 100u;
    u32 low = (total_bytes * 70u) / 100u;

    if (used_bytes > high) {
        u32 delta = (used_bytes - high) / 1024u;
        if (delta == 0) {
            delta = 1;
        }
        if (g_swap_used_kb + delta > g_swap_max_kb) {
            g_swap_used_kb = g_swap_max_kb;
        } else {
            g_swap_used_kb += delta;
        }
    } else if (used_bytes < low) {
        u32 back = (low - used_bytes) / 2048u;
        if (back == 0) {
            back = 1;
        }
        if (back > g_swap_used_kb) {
            g_swap_used_kb = 0;
        } else {
            g_swap_used_kb -= back;
        }
    }
}

u32 swap_used_kb(void) {
    return g_swap_used_kb;
}

u32 swap_max_kb(void) {
    return g_swap_max_kb;
}
