#include "compat_sece.h"
#include "timer.h"

#define SECE_MAX_REC 256u

typedef struct {
    u8 used;
    u32 bbf_id;
    u8 personality;
    u32 success;
    u32 crashes;
    i32 perf_delta;
    u32 trust;
    u32 last_tick;
    u8 quarantined;
} sece_rec_t;

static sece_rec_t g_rec[SECE_MAX_REC];
static sece_stats_t g_stats;

static int find_rec(u32 bbf_id, u8 personality) {
    for (u32 i = 0; i < SECE_MAX_REC; i++) {
        if (g_rec[i].used && g_rec[i].bbf_id == bbf_id && g_rec[i].personality == personality) return (int)i;
    }
    return -1;
}

static int alloc_rec(void) {
    for (u32 i = 0; i < SECE_MAX_REC; i++) {
        if (!g_rec[i].used) {
            g_rec[i].used = 1;
            return (int)i;
        }
    }
    return -1;
}

static sece_rec_t* touch(u32 bbf_id, u8 personality) {
    int idx = find_rec(bbf_id, personality);
    if (idx < 0) idx = alloc_rec();
    if (idx < 0) return 0;
    sece_rec_t* r = &g_rec[idx];
    if (r->bbf_id != bbf_id || r->personality != personality) {
        r->bbf_id = bbf_id;
        r->personality = personality;
        r->success = 0;
        r->crashes = 0;
        r->perf_delta = 0;
        r->trust = 1;
        r->last_tick = timer_ticks();
        r->quarantined = 0;
        g_stats.record_count++;
    }
    return r;
}

void compat_sece_init(void) {
    for (u32 i = 0; i < SECE_MAX_REC; i++) g_rec[i].used = 0;
    g_stats.record_count = 0;
    g_stats.quarantined_count = 0;
}

void compat_sece_tick(void) {
    u32 now = timer_ticks();
    for (u32 i = 0; i < SECE_MAX_REC; i++) {
        if (!g_rec[i].used) continue;
        if ((now - g_rec[i].last_tick) > 200u) {
            if (g_rec[i].trust > 0u) g_rec[i].trust--;
            if (g_rec[i].perf_delta > 0) g_rec[i].perf_delta--;
            if (g_rec[i].perf_delta < 0) g_rec[i].perf_delta++;
            g_rec[i].last_tick = now;
        }
    }
}

void compat_sece_success(u32 bbf_id, compat_personality_t personality) {
    sece_rec_t* r = touch(bbf_id, (u8)personality);
    if (!r) return;
    r->success++;
    if (r->perf_delta < 100) r->perf_delta++;
    if (r->trust < 100u) r->trust++;
    r->last_tick = timer_ticks();
}

void compat_sece_failure(u32 bbf_id, compat_personality_t personality) {
    sece_rec_t* r = touch(bbf_id, (u8)personality);
    if (!r) return;
    if (r->perf_delta > -100) r->perf_delta--;
    if (r->trust > 0u) r->trust--;
    r->last_tick = timer_ticks();
}

void compat_sece_crash(u32 bbf_id, compat_personality_t personality, u32 reason) {
    sece_rec_t* r = touch(bbf_id, (u8)personality);
    if (!r) return;
    r->crashes++;
    if (r->perf_delta > -100) r->perf_delta -= 2;
    if (r->trust > 0u) r->trust--;
    if (!r->quarantined && (reason == 0xDEADu || r->crashes > r->success + 2u)) {
        r->quarantined = 1;
        g_stats.quarantined_count++;
    }
    r->last_tick = timer_ticks();
}

int compat_sece_profile(u32 bbf_id, compat_personality_t personality, sece_profile_t* out) {
    if (!out) return -1;
    int idx = find_rec(bbf_id, (u8)personality);
    if (idx < 0) {
        out->valid = 0;
        return -1;
    }
    sece_rec_t* r = &g_rec[idx];
    out->valid = 1;
    out->success = r->success;
    out->crashes = r->crashes;
    out->perf_delta = r->perf_delta;
    out->trust = r->trust;
    return 0;
}

void compat_sece_stats(sece_stats_t* out) {
    if (!out) return;
    *out = g_stats;
}

