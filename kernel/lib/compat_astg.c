#include "compat_astg.h"
#include "timer.h"

#define ASTG_CHAIN_MAX 256u
#define ASTG_EDGE_MAX 1024u
#define ASTG_PROMOTE_THRESHOLD 10u
#define ASTG_QUARANTINE_BIAS 3u

typedef struct {
    u8 used;
    u32 key;
    u32 freq;
    u32 avg_latency;
    u32 err_count;
    u8 promoted;
    u8 quarantined;
    u8 personality;
} chain_t;

typedef struct {
    u8 used;
    u32 key;
    u16 from_nr;
    u16 to_nr;
    u32 weight;
    u32 fail_count;
    u32 last_tick;
    u8 personality;
} edge_t;

static chain_t g_chain[ASTG_CHAIN_MAX];
static edge_t g_edge[ASTG_EDGE_MAX];
static astg_stats_t g_stats;

static u32 hash3(u32 a, u32 b, u32 c) {
    u32 x = 2166136261u;
    x ^= a; x *= 16777619u;
    x ^= b; x *= 16777619u;
    x ^= c; x *= 16777619u;
    return x ^ (x >> 13);
}

static chain_t* touch_chain(u8 personality, u16 from_nr, u16 to_nr) {
    u32 key = hash3(personality, from_nr, to_nr);
    u32 free_slot = ASTG_CHAIN_MAX;
    for (u32 i = 0; i < ASTG_CHAIN_MAX; i++) {
        if (g_chain[i].used && g_chain[i].key == key) return &g_chain[i];
        if (!g_chain[i].used && free_slot == ASTG_CHAIN_MAX) free_slot = i;
    }
    if (free_slot == ASTG_CHAIN_MAX) free_slot = key % ASTG_CHAIN_MAX;
    g_chain[free_slot].used = 1;
    g_chain[free_slot].key = key;
    g_chain[free_slot].freq = 0;
    g_chain[free_slot].avg_latency = 0;
    g_chain[free_slot].err_count = 0;
    g_chain[free_slot].promoted = 0;
    g_chain[free_slot].quarantined = 0;
    g_chain[free_slot].personality = personality;
    return &g_chain[free_slot];
}

static edge_t* touch_edge(u8 personality, u16 from_nr, u16 to_nr) {
    u32 key = hash3(personality, from_nr, to_nr);
    u32 free_slot = ASTG_EDGE_MAX;
    for (u32 i = 0; i < ASTG_EDGE_MAX; i++) {
        if (g_edge[i].used && g_edge[i].key == key) return &g_edge[i];
        if (!g_edge[i].used && free_slot == ASTG_EDGE_MAX) free_slot = i;
    }
    if (free_slot == ASTG_EDGE_MAX) free_slot = key % ASTG_EDGE_MAX;
    g_edge[free_slot].used = 1;
    g_edge[free_slot].key = key;
    g_edge[free_slot].from_nr = from_nr;
    g_edge[free_slot].to_nr = to_nr;
    g_edge[free_slot].weight = 0;
    g_edge[free_slot].fail_count = 0;
    g_edge[free_slot].last_tick = timer_ticks();
    g_edge[free_slot].personality = personality;
    return &g_edge[free_slot];
}

void compat_astg_init(void) {
    for (u32 i = 0; i < ASTG_CHAIN_MAX; i++) g_chain[i].used = 0;
    for (u32 i = 0; i < ASTG_EDGE_MAX; i++) g_edge[i].used = 0;
    g_stats.chain_count = 0;
    g_stats.edge_count = 0;
    g_stats.promoted_count = 0;
    g_stats.quarantined_count = 0;
}

void compat_astg_tick(void) {
    /* Light decay to keep model adaptive */
    for (u32 i = 0; i < ASTG_CHAIN_MAX; i++) {
        if (!g_chain[i].used) continue;
        if (g_chain[i].freq > 0u) g_chain[i].freq--;
        if (g_chain[i].avg_latency > 0u) g_chain[i].avg_latency--;
    }
    for (u32 i = 0; i < ASTG_EDGE_MAX; i++) {
        if (!g_edge[i].used) continue;
        if (g_edge[i].weight > 0u) g_edge[i].weight--;
    }
}

astg_feedback_t compat_astg_record(compat_personality_t personality, u16 from_nr, u16 to_nr, u32 latency, u8 failed) {
    astg_feedback_t fb;
    fb.promoted = 0;
    fb.quarantined = 0;
    fb.hot_score = 0;
    fb.reserved = 0;

    chain_t* c = touch_chain((u8)personality, from_nr, to_nr);
    edge_t* e = touch_edge((u8)personality, from_nr, to_nr);
    if (!c || !e) return fb;

    c->freq++;
    c->avg_latency = (c->avg_latency == 0u) ? latency : ((c->avg_latency * 7u + latency) / 8u);
    if (failed) c->err_count++;

    e->weight++;
    if (failed) e->fail_count++;
    e->last_tick = timer_ticks();

    if (!c->promoted && !c->quarantined &&
        c->freq >= ASTG_PROMOTE_THRESHOLD &&
        c->avg_latency <= 32u &&
        c->err_count < 3u) {
        c->promoted = 1;
        g_stats.promoted_count++;
        fb.promoted = 1;
    }

    if (!c->quarantined && c->err_count > (c->freq / 2u) + ASTG_QUARANTINE_BIAS) {
        c->quarantined = 1;
        g_stats.quarantined_count++;
        fb.quarantined = 1;
    }

    fb.hot_score = c->freq + (e->weight / 2u);
    g_stats.chain_count++;
    g_stats.edge_count++;
    return fb;
}

u16 compat_astg_predict_next(compat_personality_t personality, u16 current_nr) {
    u32 best_weight = 0u;
    u16 best_to = current_nr;
    for (u32 i = 0; i < ASTG_EDGE_MAX; i++) {
        if (!g_edge[i].used) continue;
        if (g_edge[i].personality != (u8)personality) continue;
        if (g_edge[i].from_nr != current_nr) continue;
        if (g_edge[i].weight > best_weight) {
            best_weight = g_edge[i].weight;
            best_to = g_edge[i].to_nr;
        }
    }
    return best_to;
}

void compat_astg_quarantine_personality(compat_personality_t personality) {
    for (u32 i = 0; i < ASTG_CHAIN_MAX; i++) {
        if (!g_chain[i].used) continue;
        if (g_chain[i].personality != (u8)personality) continue;
        if (!g_chain[i].quarantined) {
            g_chain[i].quarantined = 1;
            g_stats.quarantined_count++;
        }
    }
}

void compat_astg_stats(astg_stats_t* out) {
    if (!out) return;
    *out = g_stats;
}

