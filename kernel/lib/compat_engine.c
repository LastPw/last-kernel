#include "compat_engine.h"
#include "compat_astg.h"
#include "compat_bbf.h"
#include "compat_mmul.h"
#include "compat_sece.h"
#include "compat_dllsync.h"
#include "compat_bridge.h"
#include "kshield.h"
#include "namespace.h"
#include "resource_monitor.h"
#include "rust_bridge.h"
#include "scheduler.h"
#include "string.h"
#include "timer.h"

#define CE_SYSCALL_SPACE 512u
#define CE_NATIVE_SPACE 64u
#define CE_MAX_PROC 64u
#define CE_MAX_CACHE 256u
#define CE_MAX_CHAIN 256u
#define CE_MAX_EDGE 1024u
#define CE_MAX_REC 256u
#define CE_MAX_UOBJ 256u
#define CE_MAX_HANDLE 512u
#define CE_MAX_CLUSTER 96u

#define CE_CHAIN_PROMOTE_THRESHOLD 12u
#define CE_CHAIN_QUARANTINE_ERR_BIAS 3u
#define CE_RELEARN_PERIOD_TICKS 100u
#define CE_CACHE_PRUNE_PERIOD 5u
#define CE_SYNC_QUICK_SAMPLES 12u
#define CE_SYNC_DEEP_SAMPLES 48u
#define CE_AUTOSYNC_PERIOD 64u
#define CE_SYNC_GUARD_FAIL_THRESHOLD 8u
#define CE_SYNC_GUARD_REBIND_COOLDOWN 128u
#define CE_SYNC_GUARD_LOW_CONFIDENCE 35u
#define CE_SYNC_BURST_WINDOW 32u
#define CE_SYNC_BURST_LIMIT 28u

typedef struct {
    u8 used;
    u32 task_id;
    compat_personality_t personality;
    u32 cmc_id;
    u32 bbf_id;
    u32 exec_count;
    u32 crash_count;
    u32 predicted_mem_kb;
    u32 predicted_threads;
    u32 io_burst_score;
    u32 last_syscall;
    u32 astg_hot;
    u32 dispatch_hits;
    u32 dispatch_miss;
    u32 sync_score;
    u32 sync_failures;
    u32 last_sync_tick;
    u32 bridge_confidence;
    u32 bridge_fast_hits;
    u32 bridge_slow_hits;
    u32 bridge_low_confidence;
    u32 fallback_execs;
    u32 guard_blocks;
    u32 last_native_nr;
    u32 guard_level;
    u32 burst_window_tick;
    u32 burst_count;
} ce_proc_t;

typedef struct {
    u8 used;
    u32 key;
    u16 native_nr;
    u16 generation;
    u32 hits;
} ce_cache_t;

typedef struct {
    u8 used;
    u32 key;
    u32 freq;
    u32 avg_latency;
    u32 err_count;
    u8 promoted;
    u8 quarantined;
} ce_chain_t;

typedef struct {
    u8 used;
    u32 key;
    u16 from_nr;
    u16 to_nr;
    u32 weight;
    u32 fail_count;
    u32 last_tick;
} ce_edge_t;

typedef struct {
    u8 used;
    u32 bbf_id;
    compat_personality_t personality;
    u32 success;
    u32 crashes;
    i32 perf_delta;
    u32 trust;
    u32 last_tick;
} ce_record_t;

typedef struct {
    u8 used;
    u32 uobj_id;
    u32 owner_task;
    u32 semantics;
    u32 size_kb;
    u32 commit_kb;
    u32 faults;
} ce_uobj_t;

typedef struct {
    u8 used;
    u32 handle_id;
    u32 task_id;
    u32 object_id;
    u32 flags;
} ce_handle_t;

typedef struct {
    u8 used;
    u32 cluster_id;
    u32 seed_hash;
    u32 motif_hash;
    u32 confidence;
} ce_cluster_t;

static ce_proc_t g_proc[CE_MAX_PROC];
static ce_cache_t g_cache[CE_MAX_CACHE];
static ce_chain_t g_chain[CE_MAX_CHAIN];
static ce_edge_t g_edge[CE_MAX_EDGE];
static ce_record_t g_record[CE_MAX_REC];
static ce_uobj_t g_uobj[CE_MAX_UOBJ];
static ce_handle_t g_handle[CE_MAX_HANDLE];
static ce_cluster_t g_cluster[CE_MAX_CLUSTER];
static compat_status_t g_status;

static u16 g_lut_lastos[CE_SYSCALL_SPACE];
static u16 g_lut_linux[CE_SYSCALL_SPACE];
static u16 g_lut_windows[CE_SYSCALL_SPACE];
static u8 g_rule_cost[CE_SYSCALL_SPACE];

static u32 g_generation = 1u;
static u32 g_tick_last_relearn = 0u;
static u32 g_tick_counter = 0u;
static u32 g_autosync_cursor = 0u;
static u32 g_cmc_next = 1u;
static u32 g_uobj_next = 1u;
static u32 g_handle_next = 1u;
static u32 g_cluster_next = 1u;

static u32 hash3(u32 a, u32 b, u32 c) {
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

static u32 clamp_u32(u32 value, u32 min_v, u32 max_v) {
    if (value < min_v) return min_v;
    if (value > max_v) return max_v;
    return value;
}

static u16 rd16(const u8* p) {
    return (u16)p[0] | ((u16)p[1] << 8u);
}

static u32 rd32(const u8* p) {
    return (u32)p[0] |
        ((u32)p[1] << 8u) |
        ((u32)p[2] << 16u) |
        ((u32)p[3] << 24u);
}

static int probe_pe(const u8* data, u32 len, compat_binary_probe_t* out) {
    if (len < 0x100u) return -1;
    if (data[0] != 'M' || data[1] != 'Z') return -1;

    u32 pe_off = rd32(data + 0x3Cu);
    if (pe_off + 0x60u >= len) return -1;
    if (data[pe_off + 0] != 'P' || data[pe_off + 1] != 'E' || data[pe_off + 2] != 0 || data[pe_off + 3] != 0) {
        return -1;
    }

    u16 machine = rd16(data + pe_off + 4u);
    u16 nsec = rd16(data + pe_off + 6u);
    u16 opt_size = rd16(data + pe_off + 20u);
    u16 magic = rd16(data + pe_off + 24u);
    u32 ep = rd32(data + pe_off + 40u);
    u32 image_base = rd32(data + pe_off + 52u);
    u16 subsys = 0u;

    if (opt_size >= 0x46u && pe_off + 24u + 0x46u <= len) {
        subsys = rd16(data + pe_off + 24u + 0x44u);
    }

    out->valid = 1u;
    out->kind = (magic == 0x20Bu || machine == 0x8664u) ? COMPAT_BIN_PE64 : COMPAT_BIN_PE32;
    out->personality_hint = COMPAT_PERS_WINDOWS;
    out->arch_bits = (out->kind == COMPAT_BIN_PE64) ? 64u : 32u;
    out->entry_point = ep;
    out->image_base_lo = image_base;
    out->section_or_ph_count = nsec;
    out->subsystem_hint = subsys;
    out->abi_hint = machine;
    return 0;
}

static int probe_elf(const u8* data, u32 len, compat_binary_probe_t* out) {
    if (len < 64u) return -1;
    if (data[0] != 0x7Fu || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') return -1;

    u8 cls = data[4];
    u16 machine = rd16(data + 18u);
    u32 phnum = rd16(data + 56u);
    u32 abi = data[7];
    u32 ep = rd32(data + 24u);

    out->valid = 1u;
    out->kind = (cls == 2u) ? COMPAT_BIN_ELF64 : COMPAT_BIN_ELF32;
    out->personality_hint = COMPAT_PERS_LINUX;
    out->arch_bits = (cls == 2u) ? 64u : 32u;
    out->entry_point = ep;
    out->image_base_lo = 0u;
    out->section_or_ph_count = phnum;
    out->subsystem_hint = 0u;
    out->abi_hint = machine ^ (abi << 16u);
    return 0;
}

static void init_translation_tables(void) {
    for (u32 i = 0; i < CE_SYSCALL_SPACE; i++) {
        /* Personality-local mapping with deterministic skew. */
        g_lut_lastos[i] = (u16)(i % CE_NATIVE_SPACE);
        g_lut_linux[i] = (u16)((i * 5u + 7u) % CE_NATIVE_SPACE);
        g_lut_windows[i] = (u16)((i * 9u + 13u) % CE_NATIVE_SPACE);

        /* Cost model: lower is better for fast path promotion. */
        g_rule_cost[i] = (u8)(1u + ((i * 3u + 11u) % 17u));
    }
}

static u16* lut_by_personality(compat_personality_t personality) {
    if (personality == COMPAT_PERS_WINDOWS) return g_lut_windows;
    if (personality == COMPAT_PERS_LINUX) return g_lut_linux;
    return g_lut_lastos;
}

static int find_proc(u32 task_id) {
    for (u32 i = 0; i < CE_MAX_PROC; i++) {
        if (g_proc[i].used && g_proc[i].task_id == task_id) return (int)i;
    }
    return -1;
}

static int alloc_proc(void) {
    for (u32 i = 0; i < CE_MAX_PROC; i++) {
        if (!g_proc[i].used) {
            g_proc[i].used = 1;
            g_proc[i].last_syscall = 0xFFFFFFFFu;
            return (int)i;
        }
    }
    return -1;
}

static int ensure_proc(u32 task_id) {
    int idx = find_proc(task_id);
    if (idx >= 0) return idx;

    idx = alloc_proc();
    if (idx < 0) return -1;

    g_proc[idx].task_id = task_id;
    g_proc[idx].personality = COMPAT_PERS_LASTOS;
    g_proc[idx].cmc_id = g_cmc_next++;
    g_proc[idx].bbf_id = hash3(task_id, 0x4C415354u, g_proc[idx].cmc_id);
    g_proc[idx].predicted_mem_kb = 512u;
    g_proc[idx].predicted_threads = 1u;
    g_proc[idx].io_burst_score = 0u;
    g_proc[idx].sync_score = 50u;
    g_proc[idx].sync_failures = 0u;
    g_proc[idx].last_sync_tick = 0u;
    g_status.tracked_processes++;
    g_status.cmc_count++;
    return idx;
}

static ce_record_t* touch_record(u32 bbf_id, compat_personality_t personality) {
    u32 free_slot = CE_MAX_REC;
    for (u32 i = 0; i < CE_MAX_REC; i++) {
        if (g_record[i].used &&
            g_record[i].bbf_id == bbf_id &&
            g_record[i].personality == personality) {
            return &g_record[i];
        }
        if (!g_record[i].used && free_slot == CE_MAX_REC) {
            free_slot = i;
        }
    }

    if (free_slot == CE_MAX_REC) {
        free_slot = bbf_id % CE_MAX_REC;
    }

    g_record[free_slot].used = 1;
    g_record[free_slot].bbf_id = bbf_id;
    g_record[free_slot].personality = personality;
    g_record[free_slot].success = 0;
    g_record[free_slot].crashes = 0;
    g_record[free_slot].perf_delta = 0;
    g_record[free_slot].trust = 1;
    g_record[free_slot].last_tick = timer_ticks();
    g_status.sece_records++;
    return &g_record[free_slot];
}

static ce_chain_t* touch_chain(u32 key) {
    u32 free_slot = CE_MAX_CHAIN;
    for (u32 i = 0; i < CE_MAX_CHAIN; i++) {
        if (g_chain[i].used && g_chain[i].key == key) return &g_chain[i];
        if (!g_chain[i].used && free_slot == CE_MAX_CHAIN) free_slot = i;
    }

    if (free_slot == CE_MAX_CHAIN) {
        free_slot = key % CE_MAX_CHAIN;
    }

    g_chain[free_slot].used = 1;
    g_chain[free_slot].key = key;
    g_chain[free_slot].freq = 0;
    g_chain[free_slot].avg_latency = 0;
    g_chain[free_slot].err_count = 0;
    g_chain[free_slot].promoted = 0;
    g_chain[free_slot].quarantined = 0;
    return &g_chain[free_slot];
}

static ce_edge_t* touch_edge(u32 key, u16 from_nr, u16 to_nr) {
    u32 free_slot = CE_MAX_EDGE;
    for (u32 i = 0; i < CE_MAX_EDGE; i++) {
        if (g_edge[i].used && g_edge[i].key == key) return &g_edge[i];
        if (!g_edge[i].used && free_slot == CE_MAX_EDGE) free_slot = i;
    }

    if (free_slot == CE_MAX_EDGE) {
        free_slot = key % CE_MAX_EDGE;
    }

    g_edge[free_slot].used = 1;
    g_edge[free_slot].key = key;
    g_edge[free_slot].from_nr = from_nr;
    g_edge[free_slot].to_nr = to_nr;
    g_edge[free_slot].weight = 0;
    g_edge[free_slot].fail_count = 0;
    g_edge[free_slot].last_tick = timer_ticks();
    return &g_edge[free_slot];
}

static int cache_lookup(u32 key, u16* native_nr) {
    u32 start = key % CE_MAX_CACHE;
    for (u32 off = 0; off < 16u; off++) {
        u32 i = (start + off) % CE_MAX_CACHE;
        if (!g_cache[i].used) continue;
        if (g_cache[i].key == key && g_cache[i].generation == (u16)g_generation) {
            g_cache[i].hits++;
            *native_nr = g_cache[i].native_nr;
            return 0;
        }
    }
    return -1;
}

static void cache_store(u32 key, u16 native_nr) {
    u32 slot = key % CE_MAX_CACHE;
    g_cache[slot].used = 1;
    g_cache[slot].key = key;
    g_cache[slot].native_nr = native_nr;
    g_cache[slot].generation = (u16)g_generation;
    g_cache[slot].hits = 1;
}

static void cache_prune_under_pressure(u32 pressure) {
    if (pressure < 220u || (g_tick_counter % CE_CACHE_PRUNE_PERIOD) != 0u) {
        return;
    }
    for (u32 i = 0; i < CE_MAX_CACHE; i++) {
        if (!g_cache[i].used) continue;
        if (g_cache[i].hits < 3u) {
            g_cache[i].used = 0;
        } else {
            g_cache[i].hits--;
        }
    }
}

static ce_cluster_t* touch_cluster(u32 seed_hash, u32 motif_hash) {
    u32 free_slot = CE_MAX_CLUSTER;
    u32 best_slot = CE_MAX_CLUSTER;
    u32 best_delta = 0xFFFFFFFFu;

    for (u32 i = 0; i < CE_MAX_CLUSTER; i++) {
        if (!g_cluster[i].used) {
            if (free_slot == CE_MAX_CLUSTER) free_slot = i;
            continue;
        }
        u32 d0 = (g_cluster[i].seed_hash > seed_hash)
            ? (g_cluster[i].seed_hash - seed_hash)
            : (seed_hash - g_cluster[i].seed_hash);
        u32 d1 = (g_cluster[i].motif_hash > motif_hash)
            ? (g_cluster[i].motif_hash - motif_hash)
            : (motif_hash - g_cluster[i].motif_hash);
        u32 delta = d0 + d1;
        if (delta < best_delta) {
            best_delta = delta;
            best_slot = i;
        }
    }

    if (best_slot != CE_MAX_CLUSTER && best_delta < 200000u) {
        if (g_cluster[best_slot].confidence < 100u) {
            g_cluster[best_slot].confidence++;
        }
        return &g_cluster[best_slot];
    }

    if (free_slot == CE_MAX_CLUSTER) {
        free_slot = seed_hash % CE_MAX_CLUSTER;
    }

    g_cluster[free_slot].used = 1;
    g_cluster[free_slot].cluster_id = g_cluster_next++;
    g_cluster[free_slot].seed_hash = seed_hash;
    g_cluster[free_slot].motif_hash = motif_hash;
    g_cluster[free_slot].confidence = 1u;
    return &g_cluster[free_slot];
}

static ce_uobj_t* alloc_uobj(u32 owner_task, u32 semantics, u32 size_kb) {
    for (u32 i = 0; i < CE_MAX_UOBJ; i++) {
        if (!g_uobj[i].used) {
            g_uobj[i].used = 1;
            g_uobj[i].uobj_id = g_uobj_next++;
            g_uobj[i].owner_task = owner_task;
            g_uobj[i].semantics = semantics;
            g_uobj[i].size_kb = size_kb;
            g_uobj[i].commit_kb = size_kb / 4u;
            g_uobj[i].faults = 0u;
            return &g_uobj[i];
        }
    }
    return 0;
}

static void open_handle(u32 task_id, u32 object_id, u32 flags) {
    for (u32 i = 0; i < CE_MAX_HANDLE; i++) {
        if (!g_handle[i].used) {
            g_handle[i].used = 1;
            g_handle[i].handle_id = g_handle_next++;
            g_handle[i].task_id = task_id;
            g_handle[i].object_id = object_id;
            g_handle[i].flags = flags;
            return;
        }
    }
}

static void model_memory_fault(ce_proc_t* proc, u32 write_fault, u32 exec_fault) {
    for (u32 i = 0; i < CE_MAX_UOBJ; i++) {
        if (!g_uobj[i].used || g_uobj[i].owner_task != proc->task_id) continue;
        g_uobj[i].faults++;
        if (write_fault && g_uobj[i].commit_kb < g_uobj[i].size_kb) {
            g_uobj[i].commit_kb++;
        }
        if (exec_fault && proc->personality == COMPAT_PERS_WINDOWS) {
            g_uobj[i].semantics = 2u;
        }
        return;
    }
}

static int execute_native(u16 native_nr, u32 a0, u32 a1, u32 a2, u32* out_ret) {
    if (!out_ret) return -1;
    switch (native_nr & 63u) {
        case 0: *out_ret = 0; return 0;
        case 1: *out_ret = scheduler_current_task(); return 0;
        case 2: *out_ret = timer_ticks(); return 0;
        case 3: *out_ret = a0 + a1 + a2; return 0;
        case 4: *out_ret = (a0 ^ a1) + (a2 << 1); return 0;
        case 5: *out_ret = (a0 * 31u) ^ (a1 * 131u) ^ a2; return 0;
        case 6: *out_ret = (a0 | a1 | a2); return 0;
        case 7: *out_ret = (a0 & a1 & a2); return 0;
        default: *out_ret = 0xFFFFFFFFu; return -2;
    }
}

static void update_prediction_from_snapshot(ce_proc_t* proc, const resource_snapshot_t* rs) {
    u32 mem = 128u + (rs->predicted_next_alloc / 2048u);
    if (proc->personality == COMPAT_PERS_WINDOWS) mem += 128u;
    if (proc->personality == COMPAT_PERS_LINUX) mem += 64u;
    proc->predicted_mem_kb = clamp_u32(mem, 128u, 32768u);

    u32 th = rs->recommended_threads ? rs->recommended_threads : 1u;
    proc->predicted_threads = clamp_u32(th, 1u, 16u);

    u32 io = rs->io_queue_depth + (rs->net_packets / 16u);
    proc->io_burst_score = clamp_u32(io, 0u, 100u);
}

static void apply_scheduler_bias(ce_proc_t* proc, const resource_snapshot_t* rs, u32 pressure) {
    u32 sem_cost = compat_mmul_semantic_cost(proc->personality,
        proc->last_syscall == 0xFFFFFFFFu ? 0u : proc->last_syscall);
    u32 exc_cost = compat_mmul_exception_cost(proc->personality, proc->bbf_id ^ proc->last_syscall);
    u32 trend = rs->cpu_busy_percent + rs->io_queue_depth + (rs->net_packets / 8u);
    trend = clamp_u32(trend, 0u, 100u);

    u32 score = sem_cost * 3u + exc_cost + trend + pressure;
    if (score > 220u) {
        proc->predicted_threads = clamp_u32(proc->predicted_threads + 1u, 2u, 8u);
        (void)scheduler_set_priority(proc->task_id, 6);
    } else if (score > 120u) {
        proc->predicted_threads = clamp_u32(proc->predicted_threads, 3u, 12u);
        (void)scheduler_set_priority(proc->task_id, 10);
    } else {
        proc->predicted_threads = clamp_u32(proc->predicted_threads, 1u, 8u);
        (void)scheduler_set_priority(proc->task_id, 16);
    }

    if (rs->low_mem_warning) {
        if (proc->predicted_mem_kb > 512u) proc->predicted_mem_kb -= 128u;
    } else if (pressure > 60u) {
        proc->predicted_mem_kb = clamp_u32(proc->predicted_mem_kb + 64u, 128u, 32768u);
    }

    compat_mmul_adapt(proc->task_id, proc->personality, rs,
        &proc->predicted_mem_kb, &proc->predicted_threads, &proc->io_burst_score);
}

static void learn_chain(ce_proc_t* proc, u16 from_nr, u16 to_nr, u32 dt, int rc) {
    u32 chain_key = hash3((u32)proc->personality, from_nr, to_nr);
    ce_chain_t* chain = touch_chain(chain_key);
    chain->freq++;
    chain->avg_latency = (chain->avg_latency == 0u)
        ? dt
        : ((chain->avg_latency * 7u + dt) / 8u);
    if (rc != 0) chain->err_count++;

    ce_edge_t* edge = touch_edge(chain_key, from_nr, to_nr);
    edge->weight++;
    if (rc != 0) edge->fail_count++;
    edge->last_tick = timer_ticks();

    if (!chain->promoted &&
        !chain->quarantined &&
        chain->freq >= CE_CHAIN_PROMOTE_THRESHOLD &&
        chain->avg_latency <= 32u &&
        chain->err_count < 3u) {
        chain->promoted = 1;
        proc->astg_hot++;
        g_status.astg_promotions++;
    }

    if (chain->err_count > (chain->freq / 2u) + CE_CHAIN_QUARANTINE_ERR_BIAS) {
        if (!chain->quarantined) {
            chain->quarantined = 1;
            g_status.quarantined_profiles++;
        }
    }
}

static void update_cluster(ce_proc_t* proc, u16 from_nr, u16 to_nr, u8 promoted) {
    u32 motif_hash = hash3((u32)proc->personality, from_nr * 131u, to_nr * 977u);
    ce_cluster_t* cluster = touch_cluster(proc->bbf_id, motif_hash);
    if (cluster && cluster->confidence > 20u && promoted) {
        proc->predicted_threads = clamp_u32(proc->predicted_threads + 1u, 1u, 16u);
    }
}

static void update_record(ce_proc_t* proc, int rc) {
    ce_record_t* record = touch_record(proc->bbf_id, proc->personality);
    if (!record) return;

    if (rc == 0) {
        record->success++;
        if (record->perf_delta < 100) record->perf_delta++;
        if (record->trust < 100u) record->trust++;
    } else {
        record->crashes++;
        if (record->perf_delta > -100) record->perf_delta--;
        if (record->trust > 0u) record->trust--;
    }
    record->last_tick = timer_ticks();
}

static void rebuild_status_cache_entries(void) {
    u32 used = 0u;
    for (u32 i = 0; i < CE_MAX_CACHE; i++) {
        if (g_cache[i].used) used++;
    }
    g_status.cache_entries = used;
}

static u32 sync_sample_count(u32 mode) {
    return mode ? CE_SYNC_DEEP_SAMPLES : CE_SYNC_QUICK_SAMPLES;
}

static u32 compat_sync_score_from(u32 dll_score, u32 bridge_conf, u32 success_calls, u32 sampled_calls) {
    u32 call_score = sampled_calls ? ((success_calls * 100u) / sampled_calls) : 0u;
    u32 score = (dll_score * 4u + bridge_conf * 3u + call_score * 3u) / 10u;
    return clamp_u32(score, 0u, 100u);
}

static void adapt_proc_after_sync(ce_proc_t* proc, u32 final_score, u32 sampled_calls, u32 failed_calls) {
    if (final_score >= 80u) {
        proc->predicted_threads = clamp_u32(proc->predicted_threads + 1u, 1u, 16u);
        proc->predicted_mem_kb = clamp_u32(proc->predicted_mem_kb + 64u, 128u, 32768u);
        if (proc->sync_failures > 0u) proc->sync_failures--;
    } else if (final_score <= 40u) {
        if (proc->predicted_threads > 1u) proc->predicted_threads--;
        if (proc->predicted_mem_kb > 256u) proc->predicted_mem_kb -= 64u;
        proc->sync_failures += failed_calls ? failed_calls : 1u;
    }

    if (sampled_calls > 0u && failed_calls > sampled_calls / 2u) {
        proc->sync_failures += 2u;
    }
    proc->sync_score = final_score;
    proc->last_sync_tick = timer_ticks();
}

typedef enum {
    CE_GUARD_ALLOW = 0,
    CE_GUARD_FALLBACK = 1,
    CE_GUARD_BLOCK = 2
} ce_guard_action_t;

static u8 burst_guard_hit(ce_proc_t* proc, u32 now_tick) {
    if (!proc) return 0u;
    if ((now_tick - proc->burst_window_tick) > CE_SYNC_BURST_WINDOW) {
        proc->burst_window_tick = now_tick;
        proc->burst_count = 0u;
    }
    proc->burst_count++;
    return (proc->burst_count > CE_SYNC_BURST_LIMIT) ? 1u : 0u;
}

static u32 compute_guard_level(ce_proc_t* proc, u32 dll_sync_score, u32 dll_drift_score, u32 bridge_confidence, u32 sece_trust, u32 lockdown_level, u8 burst_hit) {
    u32 risk = 0u;
    if (!proc) return 0u;
    risk += proc->sync_failures * 7u;
    if (proc->sync_score < 50u) risk += (50u - proc->sync_score);
    if (dll_sync_score < 60u) risk += (60u - dll_sync_score);
    risk += dll_drift_score / 2u;
    if (bridge_confidence < 60u) risk += (60u - bridge_confidence);
    if (sece_trust < 60u) risk += (60u - sece_trust);
    risk += lockdown_level * 8u;
    if (burst_hit) risk += 18u;
    return clamp_u32(risk / 16u, 0u, 3u);
}

static ce_guard_action_t compute_guard_action(ce_proc_t* proc, u32 local_nr, u32 dll_sync_score, u32 dll_drift_score, u32 bridge_confidence, u32 sece_trust) {
    u32 lockdown = kshield_lockdown();
    u8 burst = burst_guard_hit(proc, timer_ticks());
    u32 level = compute_guard_level(proc, dll_sync_score, dll_drift_score, bridge_confidence, sece_trust, lockdown, burst);
    u8 risky_nr = (local_nr >= 448u || ((local_nr % 16u) == 13u || (local_nr % 16u) == 15u)) ? 1u : 0u;
    u32 rust_action = 0u;
    u32 rust_level = 0u;
    u32 rust_penalty = 0u;

    if (proc && rust_bridge_policy_compat_guard(
        proc->task_id,
        proc->sync_score,
        dll_sync_score,
        dll_drift_score,
        bridge_confidence,
        sece_trust,
        lockdown,
        burst ? 1u : 0u,
        proc->sync_failures,
        risky_nr ? 1u : 0u,
        &rust_action,
        &rust_level,
        &rust_penalty) == 0) {
        if (rust_level > level) level = rust_level;
        if (rust_penalty > 0u) {
            u32 inc = rust_penalty / 8u;
            if (inc == 0u) inc = 1u;
            proc->sync_failures = clamp_u32(proc->sync_failures + inc, 0u, 1000u);
        }
    }

    proc->guard_level = level;

    if (rust_action >= 2u) {
        return CE_GUARD_BLOCK;
    }
    if (level >= 3u && (risky_nr || bridge_confidence < CE_SYNC_GUARD_LOW_CONFIDENCE)) {
        return CE_GUARD_BLOCK;
    }
    if (rust_action == 1u || level >= 2u || bridge_confidence < CE_SYNC_GUARD_LOW_CONFIDENCE || proc->sync_failures >= CE_SYNC_GUARD_FAIL_THRESHOLD) {
        return CE_GUARD_FALLBACK;
    }
    return CE_GUARD_ALLOW;
}

static void guard_namespace_rebind_if_needed(ce_proc_t* proc) {
    u32 now = timer_ticks();
    if (!proc) return;
    if (proc->personality == COMPAT_PERS_LASTOS) return;
    if (proc->sync_failures < CE_SYNC_GUARD_FAIL_THRESHOLD) return;
    if ((now - proc->last_sync_tick) < CE_SYNC_GUARD_REBIND_COOLDOWN) return;

    {
        int ns_id = namespace_create();
        if (ns_id > 0 && namespace_bind_task(proc->task_id, (u32)ns_id) == 0) {
            g_status.namespace_rebinds++;
            proc->last_sync_tick = now;
        }
    }
}

void compat_engine_init(void) {
    for (u32 i = 0; i < CE_MAX_PROC; i++) g_proc[i].used = 0;
    for (u32 i = 0; i < CE_MAX_CACHE; i++) g_cache[i].used = 0;
    for (u32 i = 0; i < CE_MAX_CHAIN; i++) g_chain[i].used = 0;
    for (u32 i = 0; i < CE_MAX_EDGE; i++) g_edge[i].used = 0;
    for (u32 i = 0; i < CE_MAX_REC; i++) g_record[i].used = 0;
    for (u32 i = 0; i < CE_MAX_UOBJ; i++) g_uobj[i].used = 0;
    for (u32 i = 0; i < CE_MAX_HANDLE; i++) g_handle[i].used = 0;
    for (u32 i = 0; i < CE_MAX_CLUSTER; i++) g_cluster[i].used = 0;

    init_translation_tables();
    compat_astg_init();
    compat_bbf_init();
    compat_mmul_init();
    compat_sece_init();
    compat_dllsync_init();

    g_status.enabled = 1;
    g_status.tracked_processes = 0;
    g_status.cmc_count = 0;
    g_status.fast_path_hits = 0;
    g_status.slow_path_hits = 0;
    g_status.abi_conflicts = 0;
    g_status.astg_promotions = 0;
    g_status.bbf_profiles = 0;
    g_status.sece_records = 0;
    g_status.quarantined_profiles = 0;
    g_status.prediction_warmups = 0;
    g_status.cache_entries = 0;
    g_status.dll_sync_events = 0;
    g_status.dll_desync_events = 0;
    g_status.dll_profiles = 0;
    g_status.dll_avg_sync_score = 0;
    g_status.bridge_fast_hits = 0;
    g_status.bridge_slow_hits = 0;
    g_status.bridge_low_confidence = 0;
    g_status.fallback_execs = 0;
    g_status.guard_blocks = 0;
    g_status.namespace_rebinds = 0;

    g_generation = 1u;
    g_tick_last_relearn = timer_ticks();
    g_tick_counter = 0u;
    g_autosync_cursor = 0u;
    g_cmc_next = 1u;
    g_uobj_next = 1u;
    g_handle_next = 1u;
    g_cluster_next = 1u;

    u32 tasks = scheduler_task_count();
    tasks = clamp_u32(tasks, 0u, 8u);
    for (u32 i = 0; i < tasks; i++) {
        (void)compat_engine_bind(i, COMPAT_PERS_LASTOS, i ^ 0xABCDu);
    }
}

void compat_engine_tick(void) {
    g_tick_counter++;

    u32 now = timer_ticks();
    if ((now - g_tick_last_relearn) < CE_RELEARN_PERIOD_TICKS) return;
    g_tick_last_relearn = now;

    resource_snapshot_t rs;
    resource_monitor_snapshot(&rs);

    u32 pressure = rs.cpu_busy_percent + rs.io_queue_depth + rs.thermal_celsius;
    pressure = clamp_u32(pressure, 0u, 300u);

    for (u32 i = 0; i < CE_MAX_PROC; i++) {
        if (!g_proc[i].used) continue;
        update_prediction_from_snapshot(&g_proc[i], &rs);
        apply_scheduler_bias(&g_proc[i], &rs, pressure);
        if ((now & 3u) == 0u) {
            model_memory_fault(&g_proc[i], now & 1u, (now >> 1u) & 1u);
        }
    }

    cache_prune_under_pressure(pressure);
    compat_astg_tick();
    compat_sece_tick();
    compat_dllsync_tick();
    if ((now % CE_AUTOSYNC_PERIOD) == 0u) {
        (void)compat_engine_autosync_step(2u);
    }

    g_generation++;
    if (g_generation == 0u) g_generation = 1u;
    rebuild_status_cache_entries();
}

int compat_engine_bind(u32 task_id, compat_personality_t personality, u32 bbf_seed) {
    int idx = ensure_proc(task_id);
    if (idx < 0) return -1;

    ce_proc_t* proc = &g_proc[idx];
    proc->personality = personality;
    bbf_bind_result_t b = compat_bbf_bind_task(task_id, personality, bbf_seed ? bbf_seed : task_id);
    proc->bbf_id = b.bbf_id ? b.bbf_id : hash3(bbf_seed ? bbf_seed : task_id, (u32)personality, proc->exec_count + 1u);
    proc->exec_count++;
    proc->dispatch_hits = 0;
    proc->dispatch_miss = 0;
    proc->sync_score = 50u;
    proc->sync_failures = 0u;
    proc->last_sync_tick = timer_ticks();
    proc->bridge_confidence = 50u;
    proc->bridge_fast_hits = 0u;
    proc->bridge_slow_hits = 0u;
    proc->bridge_low_confidence = 0u;
    proc->fallback_execs = 0u;
    proc->guard_blocks = 0u;
    proc->last_native_nr = 0u;
    proc->guard_level = 0u;
    proc->burst_window_tick = timer_ticks();
    proc->burst_count = 0u;

    int ns_id = namespace_create();
    if (ns_id > 0) {
        (void)namespace_bind_task(task_id, (u32)ns_id);
    }

    ce_uobj_t* uobj = alloc_uobj(task_id, (u32)personality + 1u, 1024u);
    if (uobj) {
        open_handle(task_id, uobj->uobj_id, 1u);
    }
    compat_mmul_bind_task(task_id, personality);
    compat_dllsync_bind(task_id, personality, bbf_seed ? bbf_seed : proc->bbf_id);

    ce_record_t* rec = touch_record(proc->bbf_id, personality);
    if (rec) rec->success++;
    compat_sece_success(proc->bbf_id, personality);
    g_status.bbf_profiles++;
    g_status.prediction_warmups++;
    return 0;
}

compat_personality_t compat_engine_personality_of(u32 task_id) {
    int idx = find_proc(task_id);
    if (idx < 0) return COMPAT_PERS_LASTOS;
    return g_proc[idx].personality;
}

int compat_engine_syscall(u32 task_id, u32 foreign_nr, u32 a0, u32 a1, u32 a2, u32* out_ret) {
    int idx = ensure_proc(task_id);
    if (idx < 0) return -1;

    ce_proc_t* proc = &g_proc[idx];
    u16 local_nr = (u16)(foreign_nr & (CE_SYSCALL_SPACE - 1u));
    u32 key = hash3((u32)proc->personality, local_nr, proc->bbf_id);

    compat_dllsync_profile_t ds;
    compat_bridge_profile_t bp;
    sece_profile_t sp;
    compat_bridge_result_t br;

    u32 dll_sync_score = 50u;
    u32 dll_drift_score = 0u;
    u32 bridge_confidence = 50u;
    u32 sece_trust = 50u;

    if (compat_dllsync_profile(task_id, &ds) == 0 && ds.valid) {
        dll_sync_score = ds.sync_score;
        dll_drift_score = ds.drift_score;
    }
    if (compat_bridge_profile(task_id, &bp) == 0 && bp.valid) {
        bridge_confidence = bp.confidence;
    }
    if (compat_sece_profile(proc->bbf_id, proc->personality, &sp) == 0 && sp.valid) {
        sece_trust = sp.trust;
    }

    ce_guard_action_t guard = compute_guard_action(proc, local_nr, dll_sync_score, dll_drift_score, bridge_confidence, sece_trust);
    guard_namespace_rebind_if_needed(proc);

    if (guard == CE_GUARD_BLOCK) {
        g_status.guard_blocks++;
        proc->guard_blocks++;
        g_status.abi_conflicts++;
        if (out_ret) *out_ret = 0xFFFFFFFDu;
        return -4;
    }

    u16 native_nr = 0u;
    u8 used_bridge = 0u;
    u8 fast_path = 0u;
    u8 fallback_mode = (guard == CE_GUARD_FALLBACK) ? 1u : 0u;

    if (compat_bridge_translate(task_id, foreign_nr, a0, a1, a2, &br) == 0 && br.translated) {
        used_bridge = 1u;
        native_nr = br.native_nr;
        fast_path = br.fast_path ? 1u : 0u;
        proc->bridge_confidence = br.confidence;
        if (br.confidence < CE_SYNC_GUARD_LOW_CONFIDENCE) {
            proc->bridge_low_confidence++;
            g_status.bridge_low_confidence++;
        }
        if (fast_path) {
            proc->bridge_fast_hits++;
            g_status.bridge_fast_hits++;
        } else {
            proc->bridge_slow_hits++;
            g_status.bridge_slow_hits++;
        }
    } else if (cache_lookup(key, &native_nr) == 0) {
        g_status.fast_path_hits++;
        proc->dispatch_hits++;
    } else {
        g_status.slow_path_hits++;
        proc->dispatch_miss++;
        u16* lut = lut_by_personality(proc->personality);
        native_nr = lut[local_nr];
        if (native_nr >= CE_NATIVE_SPACE) {
            g_status.abi_conflicts++;
            if (out_ret) *out_ret = 0xFFFFFFFEu;
            return -2;
        }
        cache_store(key, native_nr);
    }

    if (fallback_mode || native_nr >= CE_NATIVE_SPACE) {
        native_nr = g_lut_lastos[local_nr];
        proc->fallback_execs++;
        g_status.fallback_execs++;
    }
    if (native_nr >= CE_NATIVE_SPACE) {
        g_status.abi_conflicts++;
        if (out_ret) *out_ret = 0xFFFFFFFCu;
        return -2;
    }

    proc->last_native_nr = native_nr;

    u32 t0 = timer_ticks();
    int rc = execute_native(native_nr, a0, a1, a2, out_ret);
    u32 dt = timer_ticks() - t0 + (u32)(g_rule_cost[local_nr] % 7u);
    if (used_bridge && !fast_path) {
        dt += 1u;
    }

    if (proc->last_syscall != 0xFFFFFFFFu) {
        u16 from_nr = (u16)(proc->last_syscall & (CE_SYSCALL_SPACE - 1u));
        learn_chain(proc, from_nr, local_nr, dt, rc);
        astg_feedback_t af = compat_astg_record(proc->personality, from_nr, local_nr, dt, rc != 0 ? 1u : 0u);
        if (af.promoted) proc->astg_hot++;
        if (af.quarantined) g_status.quarantined_profiles++;

        u32 chain_key = hash3((u32)proc->personality, from_nr, local_nr);
        ce_chain_t* chain = touch_chain(chain_key);
        update_cluster(proc, from_nr, local_nr, chain->promoted);
        compat_bbf_note_transition(task_id, from_nr, local_nr, proc->personality);
    }

    compat_dllsync_record_call(task_id, foreign_nr, a0, a1, a2);
    if (proc->personality == COMPAT_PERS_WINDOWS) {
        u32 ds = compat_dllsync_sync_score(task_id);
        if (ds < 30u) {
            if (proc->predicted_threads > 1u) proc->predicted_threads--;
            if (proc->predicted_mem_kb > 256u) proc->predicted_mem_kb -= 64u;
        } else if (ds > 70u) {
            proc->predicted_threads = clamp_u32(proc->predicted_threads + 1u, 1u, 16u);
            proc->predicted_mem_kb = clamp_u32(proc->predicted_mem_kb + 64u, 128u, 32768u);
        }
    }

    proc->last_syscall = local_nr;
    if (bridge_confidence > proc->bridge_confidence) {
        proc->bridge_confidence = bridge_confidence;
    }
    update_record(proc, rc);
    if (rc == 0) {
        compat_sece_success(proc->bbf_id, proc->personality);
        if (proc->sync_failures > 0u && guard != CE_GUARD_FALLBACK) {
            proc->sync_failures--;
        }
    } else {
        compat_sece_failure(proc->bbf_id, proc->personality);
        proc->sync_failures++;
    }
    rebuild_status_cache_entries();
    return rc;
}

int compat_engine_sync_task(u32 task_id, u32 mode, compat_sync_report_t* out) {
    int idx = ensure_proc(task_id);
    if (idx < 0) return -1;

    ce_proc_t* proc = &g_proc[idx];
    if (out) {
        out->valid = 0u;
        out->mode = 0u;
        out->reserved = 0u;
        out->task_id = task_id;
        out->personality = proc->personality;
        out->dll_sync_score_before = 0u;
        out->dll_sync_score_after = 0u;
        out->bridge_confidence_before = 0u;
        out->bridge_confidence_after = 0u;
        out->sampled_calls = 0u;
        out->success_calls = 0u;
        out->failed_calls = 0u;
        out->predicted_mem_kb = proc->predicted_mem_kb;
        out->predicted_threads = proc->predicted_threads;
        out->final_sync_score = 0u;
        out->fallback_execs_before = proc->fallback_execs;
        out->fallback_execs_after = proc->fallback_execs;
        out->guard_blocks_before = proc->guard_blocks;
        out->guard_blocks_after = proc->guard_blocks;
        out->namespace_rebinds_before = g_status.namespace_rebinds;
        out->namespace_rebinds_after = g_status.namespace_rebinds;
    }
    if (proc->personality == COMPAT_PERS_LASTOS) {
        return -2;
    }

    compat_dllsync_profile_t dp;
    compat_bridge_profile_t bp;
    u32 dll_before = 0u;
    u32 bridge_before = 0u;
    u32 sampled = sync_sample_count(mode);
    u32 ok = 0u;
    u32 fail = 0u;
    u32 fallback_before = proc->fallback_execs;
    u32 guard_before = proc->guard_blocks;
    u32 rebind_before = g_status.namespace_rebinds;

    if (compat_dllsync_profile(task_id, &dp) == 0u && dp.valid) {
        dll_before = dp.sync_score;
    }
    if (compat_bridge_profile(task_id, &bp) == 0u && bp.valid) {
        bridge_before = bp.confidence;
    }

    for (u32 i = 0; i < sampled; i++) {
        u32 nr = hash3(proc->bbf_id, i, proc->last_syscall == 0xFFFFFFFFu ? 0u : proc->last_syscall) % CE_SYSCALL_SPACE;
        u32 ret = 0u;
        int rc = compat_engine_syscall(task_id, nr, i, i * 3u, i * 7u, &ret);
        if (rc == 0) ok++;
        else fail++;
    }

    if (compat_dllsync_profile(task_id, &dp) == 0u && dp.valid) {
        /* re-read post sampling */
    } else {
        dp.sync_score = dll_before;
    }
    if (compat_bridge_profile(task_id, &bp) != 0 || !bp.valid) {
        bp.confidence = bridge_before;
    }

    {
        u32 final_score = compat_sync_score_from(dp.sync_score, bp.confidence, ok, sampled);
        adapt_proc_after_sync(proc, final_score, sampled, fail);
        if (fail > ok) {
            proc->sync_failures += (fail - ok);
        }
        if (fail >= sampled / 2u) {
            g_status.abi_conflicts += fail / 4u;
        }

        if (out) {
            out->valid = 1u;
            out->mode = mode ? 1u : 0u;
            out->reserved = 0u;
            out->task_id = task_id;
            out->personality = proc->personality;
            out->dll_sync_score_before = dll_before;
            out->dll_sync_score_after = dp.sync_score;
            out->bridge_confidence_before = bridge_before;
            out->bridge_confidence_after = bp.confidence;
            out->sampled_calls = sampled;
            out->success_calls = ok;
            out->failed_calls = fail;
            out->predicted_mem_kb = proc->predicted_mem_kb;
            out->predicted_threads = proc->predicted_threads;
            out->final_sync_score = final_score;
            out->fallback_execs_before = fallback_before;
            out->fallback_execs_after = proc->fallback_execs;
            out->guard_blocks_before = guard_before;
            out->guard_blocks_after = proc->guard_blocks;
            out->namespace_rebinds_before = rebind_before;
            out->namespace_rebinds_after = g_status.namespace_rebinds;
        }
    }

    return 0;
}

u32 compat_engine_autosync_step(u32 budget) {
    if (budget == 0u) budget = 1u;
    u32 synced = 0u;

    for (u32 n = 0; n < CE_MAX_PROC && synced < budget; n++) {
        u32 i = (g_autosync_cursor + n) % CE_MAX_PROC;
        if (!g_proc[i].used) continue;
        if (g_proc[i].personality == COMPAT_PERS_LASTOS) continue;

        u32 age = timer_ticks() - g_proc[i].last_sync_tick;
        if (age < CE_AUTOSYNC_PERIOD && g_proc[i].sync_failures == 0u) {
            continue;
        }

        compat_sync_report_t rep;
        if (compat_engine_sync_task(g_proc[i].task_id, 0u, &rep) == 0) {
            synced++;
        }
    }

    g_autosync_cursor = (g_autosync_cursor + budget) % CE_MAX_PROC;
    return synced;
}

int compat_engine_probe_binary(const u8* data, u32 len, compat_binary_probe_t* out) {
    if (!out || !data || len < 16u) return -1;
    out->valid = 0u;
    out->kind = COMPAT_BIN_UNKNOWN;
    out->personality_hint = COMPAT_PERS_LASTOS;
    out->arch_bits = 0u;
    out->entry_point = 0u;
    out->image_base_lo = 0u;
    out->section_or_ph_count = 0u;
    out->subsystem_hint = 0u;
    out->abi_hint = 0u;

    if (probe_pe(data, len, out) == 0) return 0;
    if (probe_elf(data, len, out) == 0) return 0;
    return -2;
}

int compat_engine_prepare_task_from_probe(u32 task_id, const compat_binary_probe_t* probe) {
    if (!probe || !probe->valid) return -1;
    compat_personality_t p = probe->personality_hint;
    if (p != COMPAT_PERS_WINDOWS && p != COMPAT_PERS_LINUX) {
        p = COMPAT_PERS_LASTOS;
    }
    if (compat_engine_bind(task_id, p, probe->abi_hint ^ probe->entry_point ^ probe->section_or_ph_count) != 0) {
        return -2;
    }

    {
        int idx = find_proc(task_id);
        if (idx >= 0) {
            ce_proc_t* proc = &g_proc[idx];
            u32 bits_bias = (probe->arch_bits == 64u) ? 384u : 192u;
            proc->predicted_mem_kb = clamp_u32(512u + bits_bias + (probe->section_or_ph_count * 8u), 256u, 32768u);
            proc->predicted_threads = clamp_u32(1u + (probe->section_or_ph_count / 6u), 1u, 16u);
            if (probe->kind == COMPAT_BIN_PE64 || probe->kind == COMPAT_BIN_ELF64) {
                proc->predicted_threads = clamp_u32(proc->predicted_threads + 1u, 1u, 16u);
            }
        }
    }
    return 0;
}

int compat_engine_prepare_task_from_kind(u32 task_id, compat_binary_kind_t kind) {
    compat_binary_probe_t probe;
    probe.valid = 1u;
    probe.kind = kind;
    probe.arch_bits = (kind == COMPAT_BIN_PE64 || kind == COMPAT_BIN_ELF64) ? 64u : 32u;
    probe.entry_point = 0x1000u;
    probe.image_base_lo = 0x400000u;
    probe.section_or_ph_count = 8u;
    probe.subsystem_hint = 0u;
    probe.abi_hint = 0x100u + (u32)kind;

    if (kind == COMPAT_BIN_PE32 || kind == COMPAT_BIN_PE64) {
        probe.personality_hint = COMPAT_PERS_WINDOWS;
    } else if (kind == COMPAT_BIN_ELF32 || kind == COMPAT_BIN_ELF64) {
        probe.personality_hint = COMPAT_PERS_LINUX;
    } else {
        probe.personality_hint = COMPAT_PERS_LASTOS;
    }
    return compat_engine_prepare_task_from_probe(task_id, &probe);
}

void compat_engine_status(compat_status_t* out) {
    if (!out) return;
    *out = g_status;
    astg_stats_t as;
    bbf_stats_t bbf;
    sece_stats_t ss;
    compat_astg_stats(&as);
    compat_bbf_stats(&bbf);
    compat_sece_stats(&ss);
    compat_dllsync_stats_t ds;
    compat_bridge_stats_t cbs;
    compat_dllsync_stats(&ds);
    compat_bridge_stats(&cbs);
    out->astg_promotions = as.promoted_count;
    out->quarantined_profiles = ss.quarantined_count + as.quarantined_count;
    out->bbf_profiles = bbf.profile_count;
    out->sece_records = ss.record_count;
    out->dll_sync_events = ds.sync_events;
    out->dll_desync_events = ds.desync_events;
    out->dll_profiles = ds.profiles;
    out->dll_avg_sync_score = ds.avg_sync_score;
    if (out->bridge_fast_hits < cbs.translate_hits) out->bridge_fast_hits = cbs.translate_hits;
    if (out->bridge_slow_hits < cbs.translate_miss) out->bridge_slow_hits = cbs.translate_miss;
}

int compat_engine_profile(u32 task_id, compat_profile_t* out) {
    if (!out) return -1;
    int idx = find_proc(task_id);
    if (idx < 0) {
        out->valid = 0;
        out->sync_score = 0u;
        out->sync_failures = 0u;
        out->bridge_confidence = 0u;
        out->bridge_fast_hits = 0u;
        out->bridge_slow_hits = 0u;
        out->bridge_last_native = 0u;
        out->guard_level = 0u;
        return -1;
    }

    ce_proc_t* proc = &g_proc[idx];
    out->valid = 1;
    out->personality = proc->personality;
    out->cmc_id = proc->cmc_id;
    out->bbf_id = proc->bbf_id;
    out->astg_hot = proc->astg_hot;
    out->exec_count = proc->exec_count;
    out->crash_count = proc->crash_count;
    out->predicted_mem_kb = proc->predicted_mem_kb;
    out->predicted_threads = proc->predicted_threads;
    out->io_burst_score = proc->io_burst_score;
    out->sync_score = proc->sync_score;
    out->sync_failures = proc->sync_failures;
    out->bridge_confidence = proc->bridge_confidence;
    out->bridge_fast_hits = proc->bridge_fast_hits;
    out->bridge_slow_hits = proc->bridge_slow_hits;
    out->bridge_last_native = proc->last_native_nr;
    out->guard_level = proc->guard_level;
    compat_dllsync_profile_t ds;
    if (compat_dllsync_profile(task_id, &ds) == 0 && ds.valid) {
        out->dll_sync_score = ds.sync_score;
        out->dll_drift_score = ds.drift_score;
        out->dll_bitmap_hits = ds.bitmap_hits;
    } else {
        out->dll_sync_score = 0u;
        out->dll_drift_score = 0u;
        out->dll_bitmap_hits = 0u;
    }
    return 0;
}

void compat_engine_note_exec(u32 task_id) {
    int idx = ensure_proc(task_id);
    if (idx < 0) return;
    g_proc[idx].exec_count++;
    ce_record_t* rec = touch_record(g_proc[idx].bbf_id, g_proc[idx].personality);
    if (rec) rec->success++;
    compat_sece_success(g_proc[idx].bbf_id, g_proc[idx].personality);
}

void compat_engine_note_crash(u32 task_id, u32 reason) {
    int idx = find_proc(task_id);
    if (idx < 0) return;

    ce_proc_t* proc = &g_proc[idx];
    proc->crash_count++;

    ce_record_t* rec = touch_record(proc->bbf_id, proc->personality);
    if (rec) {
        rec->crashes++;
        if (rec->perf_delta > -100) rec->perf_delta -= 2;
        if (rec->trust > 0u) rec->trust--;
    }

    if (reason == 0xDEADu || (rec && rec->crashes > rec->success + 2u)) {
        g_status.quarantined_profiles++;
        compat_astg_quarantine_personality(proc->personality);
        proc->guard_level = 3u;
        proc->sync_failures += 2u;
        u32 last = proc->last_syscall == 0xFFFFFFFFu ? 0u : proc->last_syscall;
        u32 key_prefix = hash3((u32)proc->personality, last, 0u);
        for (u32 i = 0; i < CE_MAX_CHAIN; i++) {
            if (!g_chain[i].used) continue;
            if ((g_chain[i].key & 0xFFFF0000u) == (key_prefix & 0xFFFF0000u)) {
                g_chain[i].quarantined = 1;
            }
        }
    }
    compat_sece_crash(proc->bbf_id, proc->personality, reason);
    compat_dllsync_note_crash(task_id, reason);
}

void compat_engine_pal_descriptor(compat_personality_t personality, pal_descriptor_t* out) {
    if (!out) return;
    out->personality = personality;
    out->syscall_table_version = g_generation;
    out->memory_model_id = (personality == COMPAT_PERS_WINDOWS) ? 2u :
        ((personality == COMPAT_PERS_LINUX) ? 3u : 1u);
    out->exception_model_id = (personality == COMPAT_PERS_WINDOWS) ? 2u : 1u;

    out->abi.arch = 1u;
    out->abi.ptr_size = 4u;
    out->abi.calling_conv = (personality == COMPAT_PERS_WINDOWS) ? 2u : 1u;
    out->abi.errno_model = (personality == COMPAT_PERS_WINDOWS) ? 2u : 1u;
}
