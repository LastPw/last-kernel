#include "compat_dllsync.h"
#include "compat_bridge.h"
#include "timer.h"

#define DS_MAX_TASKS 4096u
#define DS_MAX_MODULES 8u
#define DS_BITMAP_WORDS 8u
#define DS_WINDOW 48u
#define DS_PHASES 6u
#define DS_RULES 128u
#define DS_SYM_PER_MOD 96u

#define DS_MOD_CORE 0u
#define DS_MOD_NTDLL 1u
#define DS_MOD_KERNEL32 2u
#define DS_MOD_WS2_32 3u
#define DS_MOD_USER32 4u

#define DS_MATCH_STRONG 3u
#define DS_MATCH_WEAK 1u

typedef struct {
    u8 used;
    u32 name_hash;
    u32 version;
    u32 import_count;
    u32 export_count;
    u32 import_hash;
    u32 export_hash;
    u32 ord_bitmap[DS_BITMAP_WORDS];
    u32 sym_hashes[DS_SYM_PER_MOD];
    u32 sym_count;
} ds_module_t;

typedef struct {
    u8 used;
    u32 src_mod_hash;
    u32 src_ordinal;
    u32 dst_mod_hash;
    u32 dst_ordinal;
    u32 src_symbol_hash;
    u32 dst_symbol_hash;
    u32 reliability;
    u32 hit_count;
    u32 miss_count;
} ds_bridge_rule_t;

typedef struct {
    u8 used;
    u32 task_id;
    compat_personality_t personality;

    u32 call_count;
    u32 crash_count;
    u32 bitmap_hits;
    u32 strict_hits;
    u32 weak_hits;
    u32 misses;

    u32 sync_score;
    u32 drift_score;
    u32 phase_score;
    u32 temporal_stability;

    u32 dll_hash;
    u32 import_hash;
    u32 export_hash;
    u32 import_count;
    u32 export_count;

    u32 last_tick;
    u32 window[DS_WINDOW];
    u32 win_pos;
    u32 phase_hist[DS_PHASES];

    u32 bitset[DS_BITMAP_WORDS];
    ds_module_t modules[DS_MAX_MODULES];
    u32 module_count;
} ds_task_t;

static ds_task_t g_task[DS_MAX_TASKS];
static ds_bridge_rule_t g_rules[DS_RULES];
static compat_dllsync_stats_t g_stats;

static u32 hash_mix(u32 a, u32 b, u32 c) {
    u32 x = 2166136261u;
    x ^= a; x *= 16777619u;
    x ^= b; x *= 16777619u;
    x ^= c; x *= 16777619u;
    x ^= (x >> 13);
    x *= 0x5bd1e995u;
    x ^= (x >> 15);
    return x;
}

static u32 clamp100(u32 v) {
    return (v > 100u) ? 100u : v;
}

static u32 min_u32(u32 a, u32 b) {
    return (a < b) ? a : b;
}

static void bit_set(u32* bits, u32 idx) {
    u32 w = idx / 32u;
    u32 b = idx % 32u;
    if (w >= DS_BITMAP_WORDS) return;
    bits[w] |= (1u << b);
}

static int bit_get(const u32* bits, u32 idx) {
    u32 w = idx / 32u;
    u32 b = idx % 32u;
    if (w >= DS_BITMAP_WORDS) return 0;
    return (bits[w] & (1u << b)) != 0u;
}

static void clear_bits(u32* bits) {
    for (u32 i = 0; i < DS_BITMAP_WORDS; i++) bits[i] = 0u;
}

static int find_task(u32 task_id) {
    for (u32 i = 0; i < DS_MAX_TASKS; i++) {
        if (g_task[i].used && g_task[i].task_id == task_id) return (int)i;
    }
    return -1;
}

static int alloc_task(u32 task_id) {
    for (u32 i = 0; i < DS_MAX_TASKS; i++) {
        if (!g_task[i].used) {
            g_task[i].used = 1u;
            g_task[i].task_id = task_id;
            return (int)i;
        }
    }
    u32 slot = task_id % DS_MAX_TASKS;
    g_task[slot].used = 1u;
    g_task[slot].task_id = task_id;
    return (int)slot;
}

static u32 name_hash4(char a, char b, char c, char d) {
    return ((u32)(u8)a) | (((u32)(u8)b) << 8u) | (((u32)(u8)c) << 16u) | (((u32)(u8)d) << 24u);
}

static u32 stable_symbol_hash(u32 mod_hash, u32 ord, u32 salt) {
    return hash_mix(mod_hash ^ 0x53594D30u, ord, salt ^ 0x00ABCDEFu);
}

static void module_seed_symbols(ds_module_t* m, u32 salt) {
    u32 n = m->export_count;
    if (n > DS_SYM_PER_MOD) n = DS_SYM_PER_MOD;
    m->sym_count = n;
    for (u32 i = 0; i < n; i++) {
        m->sym_hashes[i] = stable_symbol_hash(m->name_hash, i, salt + i * 17u);
    }
}

static void module_seed_ord_bitmap(ds_module_t* m, u32 salt) {
    clear_bits(m->ord_bitmap);
    u32 cap = min_u32(256u, m->export_count + 16u);
    for (u32 i = 0; i < cap; i++) {
        u32 ord = hash_mix(m->name_hash, salt + i, m->version) % 256u;
        bit_set(m->ord_bitmap, ord);
    }
}

/* PE-lite profile synthesis: deterministic, versioned, and module-aware. */
static void module_build(ds_module_t* m, u32 mod_name_hash, u32 seed, u32 version_bias) {
    m->used = 1u;
    m->name_hash = mod_name_hash;
    m->version = 1u + ((seed ^ version_bias) % 12u);
    m->import_count = 8u + ((seed ^ (mod_name_hash >> 3u)) % 120u);
    m->export_count = 8u + ((seed ^ (mod_name_hash << 1u)) % 120u);

    m->import_hash = hash_mix(mod_name_hash, m->import_count, seed ^ 0x494D5000u);
    m->export_hash = hash_mix(mod_name_hash, m->export_count, seed ^ 0x45585000u);

    module_seed_symbols(m, seed);
    module_seed_ord_bitmap(m, seed);
}

static void build_module_set(ds_task_t* t, u32 seed) {
    t->module_count = 0u;

    u32 core_seed = seed ^ hash_mix(t->task_id, (u32)t->personality, timer_ticks());
    module_build(&t->modules[t->module_count++], name_hash4('C','O','R','E'), core_seed, 1u);

    if (t->personality == COMPAT_PERS_WINDOWS) {
        module_build(&t->modules[t->module_count++], name_hash4('N','T','D','L'), core_seed ^ 0x1111u, 2u);
        module_build(&t->modules[t->module_count++], name_hash4('K','3','2',' '), core_seed ^ 0x2222u, 3u);
        module_build(&t->modules[t->module_count++], name_hash4('W','S','2',' '), core_seed ^ 0x3333u, 4u);
        module_build(&t->modules[t->module_count++], name_hash4('U','3','2',' '), core_seed ^ 0x4444u, 5u);
    } else if (t->personality == COMPAT_PERS_LINUX) {
        module_build(&t->modules[t->module_count++], name_hash4('L','I','B','C'), core_seed ^ 0xAAAAu, 6u);
        module_build(&t->modules[t->module_count++], name_hash4('L','D','S','O'), core_seed ^ 0xBBBBu, 7u);
    } else {
        module_build(&t->modules[t->module_count++], name_hash4('L','A','S','T'), core_seed ^ 0xCCCCu, 8u);
    }
}

static void init_rule(ds_bridge_rule_t* r,
                      u32 src_mod,
                      u32 src_ord,
                      u32 dst_mod,
                      u32 dst_ord,
                      u32 rel,
                      u32 salt) {
    r->used = 1u;
    r->src_mod_hash = src_mod;
    r->src_ordinal = src_ord;
    r->dst_mod_hash = dst_mod;
    r->dst_ordinal = dst_ord;
    r->src_symbol_hash = stable_symbol_hash(src_mod, src_ord, salt);
    r->dst_symbol_hash = stable_symbol_hash(dst_mod, dst_ord, salt ^ 0x77u);
    r->reliability = rel;
    r->hit_count = 0u;
    r->miss_count = 0u;
}

static void build_rule_db(void) {
    for (u32 i = 0; i < DS_RULES; i++) {
        g_rules[i].used = 0u;
    }

    u32 idx = 0u;
    u32 nt = name_hash4('N','T','D','L');
    u32 k3 = name_hash4('K','3','2',' ');
    u32 ws = name_hash4('W','S','2',' ');
    u32 u3 = name_hash4('U','3','2',' ');
    u32 libc = name_hash4('L','I','B','C');

    for (u32 i = 0; i < 24u && idx < DS_RULES; i++, idx++) {
        init_rule(&g_rules[idx], nt, i, libc, i + 10u, 84u, 0x1010u + i);
    }
    for (u32 i = 0; i < 24u && idx < DS_RULES; i++, idx++) {
        init_rule(&g_rules[idx], k3, i + 20u, libc, i + 40u, 82u, 0x2020u + i);
    }
    for (u32 i = 0; i < 20u && idx < DS_RULES; i++, idx++) {
        init_rule(&g_rules[idx], ws, i + 60u, libc, i + 80u, 79u, 0x3030u + i);
    }
    for (u32 i = 0; i < 20u && idx < DS_RULES; i++, idx++) {
        init_rule(&g_rules[idx], u3, i + 100u, libc, i + 120u, 74u, 0x4040u + i);
    }

    while (idx < DS_RULES) {
        u32 sm = (idx & 1u) ? nt : k3;
        u32 dm = libc;
        init_rule(&g_rules[idx], sm, (idx * 3u) % 200u, dm, (idx * 7u) % 200u, 60u + (idx % 21u), 0x5050u + idx);
        idx++;
    }
}

static ds_module_t* pick_module_for_call(ds_task_t* t, u32 foreign_nr) {
    if (t->module_count == 0u) return 0;
    u32 idx = 0u;

    if (t->personality == COMPAT_PERS_WINDOWS) {
        if ((foreign_nr % 5u) == 0u) idx = DS_MOD_NTDLL;
        else if ((foreign_nr % 5u) == 1u) idx = DS_MOD_KERNEL32;
        else if ((foreign_nr % 5u) == 2u) idx = DS_MOD_WS2_32;
        else if ((foreign_nr % 5u) == 3u) idx = DS_MOD_USER32;
        else idx = DS_MOD_CORE;
        if (idx >= t->module_count) idx = 0u;
    } else {
        idx = foreign_nr % t->module_count;
    }

    return &t->modules[idx];
}

static int rule_match_by_ord(u32 src_mod, u32 src_ord, ds_bridge_rule_t** out_rule) {
    ds_bridge_rule_t* best = 0;
    for (u32 i = 0; i < DS_RULES; i++) {
        ds_bridge_rule_t* r = &g_rules[i];
        if (!r->used) continue;
        if (r->src_mod_hash != src_mod || r->src_ordinal != src_ord) continue;
        if (!best || r->reliability > best->reliability) {
            best = r;
        }
    }
    if (!best) return -1;
    *out_rule = best;
    return 0;
}

static int rule_match_by_sym(u32 src_mod, u32 sym_hash, ds_bridge_rule_t** out_rule) {
    ds_bridge_rule_t* best = 0;
    for (u32 i = 0; i < DS_RULES; i++) {
        ds_bridge_rule_t* r = &g_rules[i];
        if (!r->used) continue;
        if (r->src_mod_hash != src_mod || r->src_symbol_hash != sym_hash) continue;
        if (!best || r->reliability > best->reliability) {
            best = r;
        }
    }
    if (!best) return -1;
    *out_rule = best;
    return 0;
}

static u32 expected_ordinal(ds_task_t* t, ds_module_t* mod, u32 foreign_nr, u32 a0, u32 a1, u32 a2) {
    u32 mix = hash_mix(foreign_nr ^ a0 ^ mod->import_hash, a1 ^ mod->export_hash, a2 ^ t->dll_hash);
    return mix % 256u;
}

static u32 phase_index(u32 ord) {
    return (ord / (256u / DS_PHASES)) % DS_PHASES;
}

static void update_window(ds_task_t* t, u32 ord) {
    t->window[t->win_pos % DS_WINDOW] = ord;
    t->win_pos = (t->win_pos + 1u) % DS_WINDOW;

    u32 p = phase_index(ord);
    if (p < DS_PHASES) {
        if (t->phase_hist[p] < 1000u) t->phase_hist[p]++;
    }
}

static u32 compute_phase_score(ds_task_t* t) {
    u32 minv = 0xFFFFFFFFu;
    u32 maxv = 0u;
    u32 sum = 0u;

    for (u32 i = 0; i < DS_PHASES; i++) {
        u32 v = t->phase_hist[i];
        if (v < minv) minv = v;
        if (v > maxv) maxv = v;
        sum += v;
    }
    if (sum == 0u) return 50u;

    u32 spread = maxv - minv;
    u32 avg = sum / DS_PHASES;
    u32 score = (avg > spread / 2u) ? (avg - spread / 2u) : 0u;
    if (score > 100u) score = 100u;
    return score;
}

static void recompute_temporal(ds_task_t* t) {
    u32 transitions = 0u;
    for (u32 i = 1u; i < DS_WINDOW; i++) {
        u32 a = t->window[(t->win_pos + DS_WINDOW - i) % DS_WINDOW];
        u32 b = t->window[(t->win_pos + DS_WINDOW - i - 1u) % DS_WINDOW];
        if ((a % 16u) == (b % 16u)) transitions++;
    }
    u32 st = (transitions * 100u) / (DS_WINDOW - 1u);
    t->temporal_stability = st;
}

static void apply_match(ds_task_t* t, int strength) {
    if (strength == (int)DS_MATCH_STRONG) {
        t->strict_hits++;
        t->bitmap_hits++;
        t->sync_score = clamp100(t->sync_score + 2u);
        if (t->drift_score > 0u) t->drift_score--;
        g_stats.sync_events++;
    } else if (strength == (int)DS_MATCH_WEAK) {
        t->weak_hits++;
        t->sync_score = clamp100(t->sync_score + 1u);
        g_stats.sync_events++;
    } else {
        t->misses++;
        if (t->sync_score > 0u) t->sync_score--;
        t->drift_score = clamp100(t->drift_score + 2u);
        g_stats.desync_events++;
    }
}

static void update_global_avg(void) {
    u32 sum = 0u;
    u32 cnt = 0u;
    for (u32 i = 0; i < DS_MAX_TASKS; i++) {
        if (!g_task[i].used) continue;
        sum += g_task[i].sync_score;
        cnt++;
    }
    g_stats.avg_sync_score = cnt ? (sum / cnt) : 0u;
}

void compat_dllsync_init(void) {
    for (u32 i = 0; i < DS_MAX_TASKS; i++) {
        g_task[i].used = 0u;
        g_task[i].task_id = 0u;
        g_task[i].personality = COMPAT_PERS_LASTOS;
        g_task[i].call_count = 0u;
        g_task[i].crash_count = 0u;
        g_task[i].bitmap_hits = 0u;
        g_task[i].strict_hits = 0u;
        g_task[i].weak_hits = 0u;
        g_task[i].misses = 0u;
        g_task[i].sync_score = 50u;
        g_task[i].drift_score = 0u;
        g_task[i].phase_score = 50u;
        g_task[i].temporal_stability = 100u;
        g_task[i].dll_hash = 0u;
        g_task[i].import_hash = 0u;
        g_task[i].export_hash = 0u;
        g_task[i].import_count = 0u;
        g_task[i].export_count = 0u;
        g_task[i].last_tick = 0u;
        g_task[i].win_pos = 0u;
        for (u32 k = 0; k < DS_WINDOW; k++) g_task[i].window[k] = 0u;
        for (u32 p = 0; p < DS_PHASES; p++) g_task[i].phase_hist[p] = 0u;
        clear_bits(g_task[i].bitset);
        g_task[i].module_count = 0u;
        for (u32 m = 0; m < DS_MAX_MODULES; m++) g_task[i].modules[m].used = 0u;
    }

    g_stats.sync_events = 0u;
    g_stats.desync_events = 0u;
    g_stats.profiles = 0u;
    g_stats.avg_sync_score = 0u;

    build_rule_db();
    compat_bridge_init();
}

void compat_dllsync_bind(u32 task_id, compat_personality_t personality, u32 seed) {
    int idx = find_task(task_id);
    if (idx < 0) idx = alloc_task(task_id);
    ds_task_t* t = &g_task[idx];

    t->personality = personality;
    t->call_count = 0u;
    t->crash_count = 0u;
    t->bitmap_hits = 0u;
    t->strict_hits = 0u;
    t->weak_hits = 0u;
    t->misses = 0u;
    t->sync_score = (personality == COMPAT_PERS_WINDOWS) ? 62u : 56u;
    t->drift_score = 0u;
    t->phase_score = 50u;
    t->temporal_stability = 100u;
    t->last_tick = timer_ticks();
    clear_bits(t->bitset);

    for (u32 k = 0; k < DS_WINDOW; k++) t->window[k] = 0u;
    t->win_pos = 0u;
    for (u32 p = 0; p < DS_PHASES; p++) t->phase_hist[p] = 0u;

    build_module_set(t, seed ? seed : hash_mix(task_id, (u32)personality, t->last_tick));

    /* Aggregate task-level import/export view from modules. */
    t->dll_hash = 0u;
    t->import_hash = 0u;
    t->export_hash = 0u;
    t->import_count = 0u;
    t->export_count = 0u;

    for (u32 i = 0; i < t->module_count; i++) {
        ds_module_t* m = &t->modules[i];
        t->dll_hash ^= m->name_hash;
        t->import_hash ^= m->import_hash;
        t->export_hash ^= m->export_hash;
        t->import_count += m->import_count;
        t->export_count += m->export_count;

        for (u32 o = 0u; o < 256u; o++) {
            if (bit_get(m->ord_bitmap, o)) bit_set(t->bitset, o);
        }
    }

    compat_bridge_bind(task_id, personality, seed, t->dll_hash);
    g_stats.profiles++;
    update_global_avg();
}

void compat_dllsync_record_call(u32 task_id, u32 foreign_nr, u32 a0, u32 a1, u32 a2) {
    int idx = find_task(task_id);
    if (idx < 0) return;

    ds_task_t* t = &g_task[idx];
    t->call_count++;

    compat_bridge_result_t br;
    int br_rc = compat_bridge_translate(task_id, foreign_nr, a0, a1, a2, &br);

    ds_module_t* mod = pick_module_for_call(t, foreign_nr);
    if (!mod) {
        if (br_rc == 0 && br.translated && br.confidence >= 70u) {
            apply_match(t, DS_MATCH_WEAK);
        } else {
            apply_match(t, 0);
        }
        return;
    }

    u32 ord = expected_ordinal(t, mod, foreign_nr, a0, a1, a2);
    u32 sym = stable_symbol_hash(mod->name_hash, ord, a0 ^ a1 ^ a2);

    int ord_hit = bit_get(mod->ord_bitmap, ord);
    int sym_hit = 0;
    for (u32 i = 0; i < mod->sym_count; i++) {
        if (mod->sym_hashes[i] == sym) {
            sym_hit = 1;
            break;
        }
    }

    ds_bridge_rule_t* rule = 0;
    int rule_ord = rule_match_by_ord(mod->name_hash, ord, &rule);
    int rule_sym = (rule_ord != 0) ? rule_match_by_sym(mod->name_hash, sym, &rule) : 0;

    if (rule_ord == 0 || rule_sym == 0) {
        if (rule->reliability >= 80u) {
            apply_match(t, DS_MATCH_STRONG);
            rule->hit_count++;
        } else {
            apply_match(t, DS_MATCH_WEAK);
            rule->hit_count++;
        }
    } else if (ord_hit || sym_hit) {
        apply_match(t, DS_MATCH_WEAK);
    } else {
        if (br_rc == 0 && br.translated && br.confidence >= 80u) {
            apply_match(t, DS_MATCH_WEAK);
        } else {
            apply_match(t, 0);
            if (rule) rule->miss_count++;
        }
    }

    if (br_rc == 0 && br.translated) {
        if (br.fast_path && br.confidence >= 88u) {
            apply_match(t, DS_MATCH_STRONG);
        } else if (br.confidence >= 72u) {
            apply_match(t, DS_MATCH_WEAK);
        }
    }

    update_window(t, ord);
    t->phase_score = compute_phase_score(t);
    recompute_temporal(t);

    /* Cross-correct sync score with phase+temporal metrics. */
    {
        u32 bridge_conf = compat_bridge_confidence(task_id);
        t->sync_score = clamp100((t->sync_score * 4u +
                                  t->phase_score * 2u +
                                  t->temporal_stability * 2u +
                                  bridge_conf * 2u) / 10u);
    }

    /* Drift rises when symbol churn outpaces stable hits. */
    if (t->misses > (t->strict_hits + t->weak_hits)) {
        t->drift_score = clamp100(t->drift_score + 1u);
    }

    t->last_tick = timer_ticks();
    update_global_avg();
}

void compat_dllsync_note_crash(u32 task_id, u32 reason) {
    int idx = find_task(task_id);
    if (idx < 0) return;

    ds_task_t* t = &g_task[idx];
    t->crash_count++;

    u32 penalty = 5u + (reason & 0xFu);
    if (t->sync_score > penalty) t->sync_score -= penalty;
    else t->sync_score = 0u;

    t->drift_score = clamp100(t->drift_score + penalty * 2u);
    if (t->phase_score > 2u) t->phase_score -= 2u;

    g_stats.desync_events += 2u;
    compat_bridge_note_crash(task_id, reason);
    update_global_avg();
}

void compat_dllsync_tick(void) {
    u32 now = timer_ticks();
    compat_bridge_tick();

    for (u32 i = 0; i < DS_MAX_TASKS; i++) {
        if (!g_task[i].used) continue;
        ds_task_t* t = &g_task[i];

        u32 age = now - t->last_tick;
        if (age > 200u) {
            if (t->sync_score > 0u) t->sync_score--;
            if (t->drift_score > 0u) t->drift_score--;
        }

        /* Rule reliability adaptation by observed hits/misses. */
        if ((now & 0x1Fu) == 0u) {
            for (u32 r = 0; r < DS_RULES; r++) {
                ds_bridge_rule_t* rule = &g_rules[r];
                if (!rule->used) continue;

                if (rule->hit_count > rule->miss_count + 8u && rule->reliability < 100u) {
                    rule->reliability++;
                } else if (rule->miss_count > rule->hit_count + 8u && rule->reliability > 30u) {
                    rule->reliability--;
                }

                if (rule->hit_count > 0u) rule->hit_count--;
                if (rule->miss_count > 0u) rule->miss_count--;
            }
        }

        /* Temporal drift pressure. */
        if (t->temporal_stability < 40u && t->drift_score < 100u) {
            t->drift_score++;
        }

        /* Keep scores bounded and coherent. */
        if (t->drift_score > t->sync_score + 40u && t->sync_score > 0u) {
            t->sync_score--;
        }
    }

    update_global_avg();
}

u32 compat_dllsync_sync_score(u32 task_id) {
    int idx = find_task(task_id);
    if (idx < 0) return 0u;
    return g_task[idx].sync_score;
}

int compat_dllsync_profile(u32 task_id, compat_dllsync_profile_t* out) {
    if (!out) return -1;

    int idx = find_task(task_id);
    if (idx < 0) {
        out->valid = 0u;
        return -1;
    }

    ds_task_t* t = &g_task[idx];
    out->valid = 1u;
    out->sync_score = t->sync_score;
    out->drift_score = t->drift_score;
    out->bitmap_hits = t->bitmap_hits;
    out->import_count = t->import_count;
    out->export_count = t->export_count;
    out->dll_hash = t->dll_hash;

    {
        compat_bridge_profile_t bp;
        if (compat_bridge_profile(task_id, &bp) == 0 && bp.valid) {
            out->sync_score = clamp100((out->sync_score * 3u + bp.confidence * 2u) / 5u);
            out->bitmap_hits += bp.fast_path_hits;
        }
    }
    return 0;
}

void compat_dllsync_stats(compat_dllsync_stats_t* out) {
    if (!out) return;
    *out = g_stats;
    {
        compat_bridge_stats_t bs;
        compat_bridge_stats(&bs);
        out->sync_events += bs.translate_hits;
        out->desync_events += bs.translate_miss;
        out->profiles += bs.profile_count;
        out->avg_sync_score = clamp100((out->avg_sync_score + bs.avg_confidence) / 2u);
    }
}
