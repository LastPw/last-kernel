#include "compat_bridge.h"
#include "timer.h"

#define CB_MAX_TASKS 4096u
#define CB_MAX_RULES 384u
#define CB_MAX_CHAIN 512u
#define CB_MAX_WINDOW 64u
#define CB_MAX_TAGS 128u
#define CB_NATIVE_SPACE 64u

#define CB_CONF_FAST 85u
#define CB_CONF_MED 60u
#define CB_CONF_LOW 35u

#define CB_PROMOTE_THRESHOLD 18u
#define CB_EVICT_THRESHOLD 48u

typedef struct {
    u8 used;
    u32 src_tag;
    u32 dst_tag;
    u16 foreign_nr;
    u16 native_nr;
    u32 score;
    u32 hit_count;
    u32 miss_count;
    u32 decay;
} cb_rule_t;

typedef struct {
    u8 used;
    u32 key;
    u32 freq;
    u32 confidence;
    u32 latency_hint;
    u32 hit_count;
    u32 miss_count;
    u16 native_hint;
    u8 promoted;
} cb_chain_t;

typedef struct {
    u8 used;
    u32 task_id;
    compat_personality_t personality;
    u32 seed;
    u32 dll_hash;
    u32 confidence;
    u32 fast_path_hits;
    u32 slow_path_hits;
    u32 crash_count;
    u32 window[CB_MAX_WINDOW];
    u32 win_pos;
    u32 last_tick;
    u32 active_chain_key;
    u32 fingerprint;
} cb_task_t;

typedef struct {
    u8 used;
    u32 raw;
    u32 normalized;
    u32 hotness;
} cb_tag_t;

static cb_rule_t g_rules[CB_MAX_RULES];
static cb_chain_t g_chains[CB_MAX_CHAIN];
static cb_task_t g_tasks[CB_MAX_TASKS];
static cb_tag_t g_tags[CB_MAX_TAGS];
static compat_bridge_stats_t g_stats;

static u32 hash_mix(u32 a, u32 b, u32 c) {
    u32 x = 2166136261u;
    x ^= a;
    x *= 16777619u;
    x ^= b;
    x *= 16777619u;
    x ^= c;
    x *= 16777619u;
    x ^= (x >> 13);
    x *= 0x5bd1e995u;
    x ^= (x >> 15);
    return x;
}

static u32 clamp_u32(u32 v, u32 lo, u32 hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static u32 min_u32(u32 a, u32 b) {
    return a < b ? a : b;
}

static u32 max_u32(u32 a, u32 b) {
    return a > b ? a : b;
}

static u32 normalize_nr(u32 nr) {
    return nr % CB_NATIVE_SPACE;
}

static u32 personality_bias(compat_personality_t p) {
    if (p == COMPAT_PERS_WINDOWS) return 0x57494E30u;
    if (p == COMPAT_PERS_LINUX) return 0x4C4E5830u;
    return 0x4C415354u;
}

static void reset_stats(void) {
    g_stats.translate_hits = 0u;
    g_stats.translate_miss = 0u;
    g_stats.chain_promotions = 0u;
    g_stats.chain_evictions = 0u;
    g_stats.profile_count = 0u;
    g_stats.avg_confidence = 0u;
}

static int find_task(u32 task_id) {
    for (u32 i = 0; i < CB_MAX_TASKS; i++) {
        if (g_tasks[i].used && g_tasks[i].task_id == task_id) return (int)i;
    }
    return -1;
}

static int alloc_task(u32 task_id) {
    for (u32 i = 0; i < CB_MAX_TASKS; i++) {
        if (!g_tasks[i].used) {
            g_tasks[i].used = 1u;
            g_tasks[i].task_id = task_id;
            return (int)i;
        }
    }
    u32 slot = task_id % CB_MAX_TASKS;
    g_tasks[slot].used = 1u;
    g_tasks[slot].task_id = task_id;
    return (int)slot;
}

static int find_tag(u32 raw) {
    for (u32 i = 0; i < CB_MAX_TAGS; i++) {
        if (g_tags[i].used && g_tags[i].raw == raw) return (int)i;
    }
    return -1;
}

static u32 push_tag(u32 raw) {
    int idx = find_tag(raw);
    if (idx >= 0) {
        if (g_tags[idx].hotness < 1000u) g_tags[idx].hotness++;
        return g_tags[idx].normalized;
    }

    u32 free_slot = CB_MAX_TAGS;
    for (u32 i = 0; i < CB_MAX_TAGS; i++) {
        if (!g_tags[i].used) {
            free_slot = i;
            break;
        }
    }
    if (free_slot == CB_MAX_TAGS) free_slot = raw % CB_MAX_TAGS;

    g_tags[free_slot].used = 1u;
    g_tags[free_slot].raw = raw;
    g_tags[free_slot].normalized = hash_mix(raw, 0x54414700u, free_slot) % 2048u;
    g_tags[free_slot].hotness = 1u;
    return g_tags[free_slot].normalized;
}

static cb_rule_t* touch_rule(u32 src_tag, u32 dst_tag, u16 foreign_nr, u16 native_nr) {
    u32 free_slot = CB_MAX_RULES;
    for (u32 i = 0; i < CB_MAX_RULES; i++) {
        cb_rule_t* r = &g_rules[i];
        if (r->used &&
            r->src_tag == src_tag &&
            r->dst_tag == dst_tag &&
            r->foreign_nr == foreign_nr) {
            return r;
        }
        if (!r->used && free_slot == CB_MAX_RULES) {
            free_slot = i;
        }
    }

    if (free_slot == CB_MAX_RULES) {
        free_slot = hash_mix(src_tag, dst_tag, foreign_nr) % CB_MAX_RULES;
    }

    cb_rule_t* r = &g_rules[free_slot];
    r->used = 1u;
    r->src_tag = src_tag;
    r->dst_tag = dst_tag;
    r->foreign_nr = foreign_nr;
    r->native_nr = native_nr;
    r->score = 40u;
    r->hit_count = 0u;
    r->miss_count = 0u;
    r->decay = 0u;
    return r;
}

static cb_chain_t* touch_chain(u32 key) {
    u32 free_slot = CB_MAX_CHAIN;
    for (u32 i = 0; i < CB_MAX_CHAIN; i++) {
        cb_chain_t* c = &g_chains[i];
        if (c->used && c->key == key) return c;
        if (!c->used && free_slot == CB_MAX_CHAIN) free_slot = i;
    }

    if (free_slot == CB_MAX_CHAIN) {
        free_slot = key % CB_MAX_CHAIN;
    }

    cb_chain_t* c = &g_chains[free_slot];
    c->used = 1u;
    c->key = key;
    c->freq = 0u;
    c->confidence = 45u;
    c->latency_hint = 0u;
    c->hit_count = 0u;
    c->miss_count = 0u;
    c->native_hint = (u16)(key % CB_NATIVE_SPACE);
    c->promoted = 0u;
    return c;
}

static void reset_task(cb_task_t* t) {
    t->confidence = 50u;
    t->fast_path_hits = 0u;
    t->slow_path_hits = 0u;
    t->crash_count = 0u;
    t->win_pos = 0u;
    t->last_tick = timer_ticks();
    t->active_chain_key = 0u;
    t->fingerprint = hash_mix(t->task_id, personality_bias(t->personality), t->seed ^ t->dll_hash);
    for (u32 i = 0; i < CB_MAX_WINDOW; i++) t->window[i] = 0u;
}

static u32 src_tag_for(cb_task_t* t, u32 foreign_nr, u32 a0, u32 a1, u32 a2) {
    u32 raw = hash_mix(foreign_nr ^ personality_bias(t->personality),
                       a0 ^ (a1 << 1u),
                       a2 ^ t->dll_hash ^ t->seed);
    return push_tag(raw);
}

static u32 dst_tag_for(cb_task_t* t, u32 src_tag, u32 foreign_nr) {
    u32 raw = hash_mix(src_tag, foreign_nr ^ t->fingerprint, t->dll_hash ^ 0xBEEF0000u);
    return push_tag(raw);
}

static u16 map_native(cb_task_t* t, u32 foreign_nr, u32 src_tag, u32 dst_tag) {
    u32 base = foreign_nr;
    if (t->personality == COMPAT_PERS_WINDOWS) {
        base = foreign_nr * 11u + 23u;
    } else if (t->personality == COMPAT_PERS_LINUX) {
        base = foreign_nr * 7u + 13u;
    } else {
        base = foreign_nr * 3u + 5u;
    }
    base ^= src_tag;
    base += dst_tag;
    return (u16)normalize_nr(base);
}

static void update_task_window(cb_task_t* t, u32 symbol) {
    t->window[t->win_pos % CB_MAX_WINDOW] = symbol;
    t->win_pos = (t->win_pos + 1u) % CB_MAX_WINDOW;
}

static u32 chain_key_of(cb_task_t* t, u32 symbol) {
    u32 prev = t->window[(t->win_pos + CB_MAX_WINDOW - 1u) % CB_MAX_WINDOW];
    u32 prev2 = t->window[(t->win_pos + CB_MAX_WINDOW - 2u) % CB_MAX_WINDOW];
    return hash_mix(prev, prev2, symbol ^ t->fingerprint);
}

static u32 chain_stability(cb_task_t* t) {
    u32 same_class = 0u;
    for (u32 i = 1u; i < CB_MAX_WINDOW; i++) {
        u32 a = t->window[(t->win_pos + CB_MAX_WINDOW - i) % CB_MAX_WINDOW];
        u32 b = t->window[(t->win_pos + CB_MAX_WINDOW - i - 1u) % CB_MAX_WINDOW];
        if ((a % 32u) == (b % 32u)) same_class++;
    }
    return (same_class * 100u) / (CB_MAX_WINDOW - 1u);
}

static u32 weighted_confidence(u32 rule_score, u32 chain_conf, u32 stability, u32 crashes) {
    u32 crash_penalty = min_u32(40u, crashes * 4u);
    u32 conf = (rule_score * 5u + chain_conf * 3u + stability * 2u) / 10u;
    if (conf > crash_penalty) conf -= crash_penalty;
    else conf = 0u;
    return clamp_u32(conf, 0u, 100u);
}

static void refresh_avg_confidence(void) {
    u32 sum = 0u;
    u32 count = 0u;
    for (u32 i = 0; i < CB_MAX_TASKS; i++) {
        if (!g_tasks[i].used) continue;
        sum += g_tasks[i].confidence;
        count++;
    }
    g_stats.avg_confidence = count ? (sum / count) : 0u;
}

static void seed_personality_rules(compat_personality_t p, u32 seed_bias) {
    u32 src_base = (p == COMPAT_PERS_WINDOWS) ? 900u : ((p == COMPAT_PERS_LINUX) ? 500u : 100u);
    u32 dst_base = (p == COMPAT_PERS_WINDOWS) ? 300u : ((p == COMPAT_PERS_LINUX) ? 700u : 200u);

    for (u32 i = 0; i < 64u; i++) {
        u32 src_tag = push_tag(src_base + i * 17u + seed_bias);
        u32 dst_tag = push_tag(dst_base + i * 19u + seed_bias);
        u16 fnr = (u16)((src_base + i * 3u) % 256u);
        u16 nnr = (u16)((dst_base + i * 5u + seed_bias) % CB_NATIVE_SPACE);

        cb_rule_t* r = touch_rule(src_tag, dst_tag, fnr, nnr);
        r->score = 68u + (i % 24u);
        r->decay = i % 5u;
    }
}

static void initialize_rules(void) {
    for (u32 i = 0; i < CB_MAX_RULES; i++) g_rules[i].used = 0u;
    seed_personality_rules(COMPAT_PERS_LASTOS, 11u);
    seed_personality_rules(COMPAT_PERS_LINUX, 29u);
    seed_personality_rules(COMPAT_PERS_WINDOWS, 47u);
}

static void reset_tables(void) {
    for (u32 i = 0; i < CB_MAX_CHAIN; i++) g_chains[i].used = 0u;
    for (u32 i = 0; i < CB_MAX_TASKS; i++) g_tasks[i].used = 0u;
    for (u32 i = 0; i < CB_MAX_TAGS; i++) g_tags[i].used = 0u;
    initialize_rules();
    reset_stats();
}

void compat_bridge_init(void) {
    reset_tables();
}

void compat_bridge_bind(u32 task_id, compat_personality_t personality, u32 seed, u32 dll_hash) {
    int idx = find_task(task_id);
    if (idx < 0) idx = alloc_task(task_id);
    cb_task_t* t = &g_tasks[idx];

    t->personality = personality;
    t->seed = seed;
    t->dll_hash = dll_hash;
    reset_task(t);

    g_stats.profile_count++;
    refresh_avg_confidence();
}

static void chain_decay_step(cb_chain_t* c) {
    if (c->freq > 0u) c->freq--;
    if (c->hit_count > 0u) c->hit_count--;
    if (c->miss_count > 0u) c->miss_count--;

    if (c->confidence > 50u) c->confidence--;
    else if (c->confidence < 50u) c->confidence++;

    if (c->promoted && c->freq < 3u) {
        c->promoted = 0u;
        c->confidence = max_u32(30u, c->confidence - 6u);
        g_stats.chain_evictions++;
    }
}

static void update_rule_reliability(cb_rule_t* r, int hit) {
    if (hit) {
        r->hit_count++;
        if (r->score < 100u) r->score++;
    } else {
        r->miss_count++;
        if (r->score > 0u) r->score--;
    }
}

static u32 predict_chain_latency(u32 chain_key, u32 conf) {
    u32 mix = hash_mix(chain_key, conf, 0x4C415445u);
    return 10u + (mix % 90u);
}

static cb_rule_t* lookup_best_rule(u32 src_tag, u32 dst_tag, u16 foreign_nr) {
    cb_rule_t* best = 0;
    for (u32 i = 0; i < CB_MAX_RULES; i++) {
        cb_rule_t* r = &g_rules[i];
        if (!r->used) continue;
        if (r->src_tag != src_tag) continue;
        if (r->foreign_nr != foreign_nr) continue;
        if (r->dst_tag != dst_tag) continue;
        if (!best || r->score > best->score) best = r;
    }
    return best;
}

static cb_rule_t* fallback_rule(u32 src_tag, u16 foreign_nr) {
    cb_rule_t* best = 0;
    for (u32 i = 0; i < CB_MAX_RULES; i++) {
        cb_rule_t* r = &g_rules[i];
        if (!r->used) continue;
        if (r->src_tag != src_tag) continue;
        if (r->foreign_nr != foreign_nr) continue;
        if (!best || r->score > best->score) best = r;
    }
    return best;
}

static u8 promoted_fast_path(cb_chain_t* c) {
    if (c->promoted) return 1u;
    if (c->freq >= CB_PROMOTE_THRESHOLD && c->confidence >= CB_CONF_MED) {
        c->promoted = 1u;
        c->confidence = min_u32(100u, c->confidence + 7u);
        g_stats.chain_promotions++;
        return 1u;
    }
    return 0u;
}

static void apply_chain_feedback(cb_chain_t* c, u32 confidence, int success) {
    if (success) {
        c->freq++;
        c->hit_count++;
        if (c->confidence < 100u) c->confidence++;
    } else {
        c->miss_count++;
        if (c->confidence > 0u) c->confidence--;
    }

    if (confidence > c->confidence) {
        c->confidence = min_u32(100u, (c->confidence * 3u + confidence) / 4u);
    } else {
        c->confidence = max_u32(10u, (c->confidence * 7u + confidence) / 8u);
    }

    c->latency_hint = predict_chain_latency(c->key, c->confidence);
    if (c->freq > CB_EVICT_THRESHOLD && c->miss_count > c->hit_count + 12u) {
        c->promoted = 0u;
        c->freq = CB_PROMOTE_THRESHOLD / 2u;
        c->confidence = 45u;
        g_stats.chain_evictions++;
    }
}

static void fill_translate_result(compat_bridge_result_t* out,
                                  u8 translated,
                                  u8 fast_path,
                                  u16 native_nr,
                                  u32 confidence,
                                  u32 source_tag,
                                  u32 target_tag,
                                  u32 chain_key) {
    if (!out) return;
    out->translated = translated;
    out->fast_path = fast_path;
    out->native_nr = native_nr;
    out->confidence = confidence;
    out->source_tag = source_tag;
    out->target_tag = target_tag;
    out->chain_key = chain_key;
}

int compat_bridge_translate(u32 task_id,
                            u32 foreign_nr,
                            u32 a0,
                            u32 a1,
                            u32 a2,
                            compat_bridge_result_t* out) {
    int idx = find_task(task_id);
    if (idx < 0) {
        fill_translate_result(out, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
        return -1;
    }

    cb_task_t* t = &g_tasks[idx];
    u32 src_tag = src_tag_for(t, foreign_nr, a0, a1, a2);
    u32 dst_tag = dst_tag_for(t, src_tag, foreign_nr);
    u16 foreign16 = (u16)(foreign_nr % 256u);

    u32 symbol = hash_mix(src_tag, dst_tag, foreign_nr ^ t->fingerprint);
    update_task_window(t, symbol);
    u32 chain_key = chain_key_of(t, symbol);
    cb_chain_t* chain = touch_chain(chain_key);
    t->active_chain_key = chain_key;

    cb_rule_t* rule = lookup_best_rule(src_tag, dst_tag, foreign16);
    u16 native_nr;
    u8 success = 0u;
    u8 fast = 0u;

    if (rule) {
        native_nr = rule->native_nr;
        update_rule_reliability(rule, 1);
        success = 1u;
    } else {
        cb_rule_t* fb = fallback_rule(src_tag, foreign16);
        if (fb && fb->score >= CB_CONF_LOW) {
            native_nr = fb->native_nr;
            update_rule_reliability(fb, 1);
            success = 1u;
            dst_tag = fb->dst_tag;
        } else {
            native_nr = map_native(t, foreign_nr, src_tag, dst_tag);
            if (fb) update_rule_reliability(fb, 0);
        }
    }

    u32 stability = chain_stability(t);
    u32 conf = weighted_confidence(rule ? rule->score : 40u,
                                   chain->confidence,
                                   stability,
                                   t->crash_count);

    apply_chain_feedback(chain, conf, success ? 1 : 0);
    fast = promoted_fast_path(chain);

    if (fast && conf >= CB_CONF_FAST) {
        t->fast_path_hits++;
        g_stats.translate_hits++;
    } else if (success) {
        t->slow_path_hits++;
        g_stats.translate_hits++;
    } else {
        t->slow_path_hits++;
        g_stats.translate_miss++;
    }

    t->confidence = clamp_u32((t->confidence * 4u + conf * 6u) / 10u, 0u, 100u);
    t->last_tick = timer_ticks();

    if (success && !rule) {
        cb_rule_t* r = touch_rule(src_tag, dst_tag, foreign16, native_nr);
        r->score = clamp_u32((r->score + conf) / 2u, 25u, 90u);
    }

    fill_translate_result(out, success, fast, native_nr, conf, src_tag, dst_tag, chain_key);
    refresh_avg_confidence();
    return success ? 0 : 1;
}

void compat_bridge_note_crash(u32 task_id, u32 reason) {
    int idx = find_task(task_id);
    if (idx < 0) return;

    cb_task_t* t = &g_tasks[idx];
    t->crash_count++;
    t->confidence = (t->confidence > 8u) ? (t->confidence - 8u) : 0u;

    if (t->active_chain_key) {
        cb_chain_t* c = touch_chain(t->active_chain_key);
        c->miss_count += 3u + (reason & 0x3u);
        if (c->confidence > 5u) c->confidence -= 5u;
        if (c->promoted && c->confidence < 40u) {
            c->promoted = 0u;
            g_stats.chain_evictions++;
        }
    }

    for (u32 i = 0; i < CB_MAX_RULES; i++) {
        cb_rule_t* r = &g_rules[i];
        if (!r->used) continue;
        if (r->score > 1u) r->score--;
    }

    refresh_avg_confidence();
}

u32 compat_bridge_confidence(u32 task_id) {
    int idx = find_task(task_id);
    if (idx < 0) return 0u;
    return g_tasks[idx].confidence;
}

static u32 chain_depth_for_task(cb_task_t* t) {
    u32 count = 0u;
    for (u32 i = 0; i < CB_MAX_CHAIN; i++) {
        if (!g_chains[i].used) continue;
        if ((g_chains[i].key ^ t->fingerprint) % 7u == 0u) count++;
    }
    return count;
}

int compat_bridge_profile(u32 task_id, compat_bridge_profile_t* out) {
    if (!out) return -1;
    int idx = find_task(task_id);
    if (idx < 0) {
        out->valid = 0u;
        return -1;
    }

    cb_task_t* t = &g_tasks[idx];
    out->valid = 1u;
    out->confidence = t->confidence;
    out->chain_depth = chain_depth_for_task(t);
    out->fast_path_hits = t->fast_path_hits;
    out->slow_path_hits = t->slow_path_hits;
    out->source_tag = t->window[(t->win_pos + CB_MAX_WINDOW - 1u) % CB_MAX_WINDOW];
    out->target_tag = t->window[(t->win_pos + CB_MAX_WINDOW - 2u) % CB_MAX_WINDOW];
    out->fingerprint = t->fingerprint;
    return 0;
}

void compat_bridge_stats(compat_bridge_stats_t* out) {
    if (!out) return;
    *out = g_stats;
}

static void rules_maintenance_tick(u32 now) {
    if ((now & 0x0Fu) != 0u) return;

    for (u32 i = 0; i < CB_MAX_RULES; i++) {
        cb_rule_t* r = &g_rules[i];
        if (!r->used) continue;

        if (r->hit_count > r->miss_count + 6u) {
            if (r->score < 100u) r->score++;
        } else if (r->miss_count > r->hit_count + 6u) {
            if (r->score > 0u) r->score--;
        }

        if (r->hit_count > 0u) r->hit_count--;
        if (r->miss_count > 0u) r->miss_count--;

        r->decay++;
        if (r->decay > 512u) {
            r->decay = 0u;
            if (r->score > 0u) r->score--;
        }
    }
}

static void chains_maintenance_tick(u32 now) {
    if ((now & 0x03u) != 0u) return;

    for (u32 i = 0; i < CB_MAX_CHAIN; i++) {
        cb_chain_t* c = &g_chains[i];
        if (!c->used) continue;
        chain_decay_step(c);
    }
}

static void tasks_maintenance_tick(u32 now) {
    for (u32 i = 0; i < CB_MAX_TASKS; i++) {
        cb_task_t* t = &g_tasks[i];
        if (!t->used) continue;

        u32 age = now - t->last_tick;
        if (age > 300u) {
            if (t->confidence > 0u) t->confidence--;
        }
        if (age > 1200u) {
            t->fast_path_hits = 0u;
            t->slow_path_hits = 0u;
            t->active_chain_key = 0u;
            for (u32 w = 0; w < CB_MAX_WINDOW; w++) t->window[w] = 0u;
        }
    }
}

static void tags_maintenance_tick(u32 now) {
    if ((now & 0x1Fu) != 0u) return;
    for (u32 i = 0; i < CB_MAX_TAGS; i++) {
        cb_tag_t* t = &g_tags[i];
        if (!t->used) continue;
        if (t->hotness > 0u) t->hotness--;
        if (t->hotness == 0u && (i % 5u) == (now % 5u)) {
            t->used = 0u;
        }
    }
}

void compat_bridge_tick(void) {
    u32 now = timer_ticks();
    rules_maintenance_tick(now);
    chains_maintenance_tick(now);
    tasks_maintenance_tick(now);
    tags_maintenance_tick(now);
    refresh_avg_confidence();
}

