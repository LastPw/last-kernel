/* ---------------------------------------------------------------------------
 * boot_log.c — Boot-time logging ring buffer
 *
 * Stores structured boot events (stage transitions, module results, policy
 * decisions, errors) with timestamps.  Complements the lower-level ktrace
 * by providing a human-readable, shell-queryable boot narrative.
 * -------------------------------------------------------------------------*/

#include "boot_log.h"
#include "timer.h"
#include "string.h"

/* ── Ring buffer ───────────────────────────────────────────────────────── */
static boot_log_entry_t g_ring[BOOT_LOG_CAP];
static u32 g_head  = 0u;
static u32 g_count = 0u;
static u8  g_inited = 0u;

/* ── Initialisation ────────────────────────────────────────────────────── */
void boot_log_init(void) {
    if (g_inited) return;
    g_head  = 0u;
    g_count = 0u;
    for (u32 i = 0u; i < BOOT_LOG_CAP; i++) {
        g_ring[i].tick   = 0u;
        g_ring[i].type   = 0u;
        g_ring[i].result = 0;
        g_ring[i].desc[0] = '\0';
        g_ring[i].detail = 0u;
    }
    g_inited = 1u;
}

/* ── Push ──────────────────────────────────────────────────────────────── */
void boot_log_push(u32 type, i32 result, const char* desc, u32 detail) {
    boot_log_entry_t* e = &g_ring[g_head];

    e->tick   = timer_ticks();
    e->type   = type;
    e->result = result;
    e->detail = detail;

    if (desc) {
        u32 i;
        for (i = 0u; i < sizeof(e->desc) - 1u && desc[i]; i++) {
            e->desc[i] = desc[i];
        }
        e->desc[i] = '\0';
    } else {
        e->desc[0] = '\0';
    }

    g_head = (g_head + 1u) % BOOT_LOG_CAP;
    if (g_count < BOOT_LOG_CAP) g_count++;
}

/* ── Stage helper ──────────────────────────────────────────────────────── */
void boot_log_stage(u32 level_idx, const char* level_name, int ok) {
    char buf[48];
    u32 i;
    for (i = 0u; i < sizeof(buf) - 1u && level_name && level_name[i]; i++) {
        buf[i] = level_name[i];
    }
    buf[i] = '\0';
    boot_log_push(ok == 0 ? BOOT_LOG_STAGE_OK : BOOT_LOG_STAGE_FAIL,
                  ok, buf, level_idx);
}

/* ── Module helper ─────────────────────────────────────────────────────── */
void boot_log_module(const char* module_name, int rc) {
    boot_log_push(rc == 0 ? BOOT_LOG_MODULE_OK : BOOT_LOG_MODULE_FAIL,
                  rc, module_name, 0u);
}

/* ── Query ─────────────────────────────────────────────────────────────── */
u32 boot_log_count(void) {
    return g_count;
}

int boot_log_get(u32 reverse_index, boot_log_entry_t* out) {
    if (!out) return -1;
    if (reverse_index >= g_count) return -1;

    /* g_head points to the slot that will be written next, so the most recent
     * entry is the slot just before g_head (with wrap-around). */
    u32 latest = (g_head == 0u) ? (BOOT_LOG_CAP - 1u) : (g_head - 1u);
    u32 idx = (latest + BOOT_LOG_CAP - reverse_index) % BOOT_LOG_CAP;
    *out = g_ring[idx];
    return 0;
}

/* ── Clear ─────────────────────────────────────────────────────────────── */
void boot_log_clear(void) {
    g_head  = 0u;
    g_count = 0u;
}
