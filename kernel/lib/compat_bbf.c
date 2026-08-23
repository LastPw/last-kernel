#include "compat_bbf.h"

#define BBF_MAX_TASK 128u
#define BBF_MAX_CLUSTER 128u

typedef struct {
    u8 used;
    u32 task_id;
    bbf_profile_t profile;
} bbf_task_entry_t;

typedef struct {
    u8 used;
    u32 cluster_id;
    u32 seed_hash;
    u32 motif_hash;
    u32 confidence;
} bbf_cluster_t;

static bbf_task_entry_t g_task[BBF_MAX_TASK];
static bbf_cluster_t g_cluster[BBF_MAX_CLUSTER];
static bbf_stats_t g_stats;
static u32 g_cluster_next = 1u;

static u32 hash3(u32 a, u32 b, u32 c) {
    u32 x = 2166136261u;
    x ^= a; x *= 16777619u;
    x ^= b; x *= 16777619u;
    x ^= c; x *= 16777619u;
    return x ^ (x >> 13);
}

static int find_task(u32 task_id) {
    for (u32 i = 0; i < BBF_MAX_TASK; i++) {
        if (g_task[i].used && g_task[i].task_id == task_id) return (int)i;
    }
    return -1;
}

static int alloc_task(void) {
    for (u32 i = 0; i < BBF_MAX_TASK; i++) {
        if (!g_task[i].used) {
            g_task[i].used = 1;
            return (int)i;
        }
    }
    return -1;
}

static bbf_cluster_t* touch_cluster(u32 seed_hash, u32 motif_hash) {
    u32 free_slot = BBF_MAX_CLUSTER;
    u32 best_slot = BBF_MAX_CLUSTER;
    u32 best_delta = 0xFFFFFFFFu;
    for (u32 i = 0; i < BBF_MAX_CLUSTER; i++) {
        if (!g_cluster[i].used) {
            if (free_slot == BBF_MAX_CLUSTER) free_slot = i;
            continue;
        }
        u32 d0 = (g_cluster[i].seed_hash > seed_hash) ? (g_cluster[i].seed_hash - seed_hash) : (seed_hash - g_cluster[i].seed_hash);
        u32 d1 = (g_cluster[i].motif_hash > motif_hash) ? (g_cluster[i].motif_hash - motif_hash) : (motif_hash - g_cluster[i].motif_hash);
        u32 delta = d0 + d1;
        if (delta < best_delta) {
            best_delta = delta;
            best_slot = i;
        }
    }
    if (best_slot != BBF_MAX_CLUSTER && best_delta < 200000u) {
        if (g_cluster[best_slot].confidence < 100u) g_cluster[best_slot].confidence++;
        return &g_cluster[best_slot];
    }
    if (free_slot == BBF_MAX_CLUSTER) free_slot = seed_hash % BBF_MAX_CLUSTER;
    g_cluster[free_slot].used = 1;
    g_cluster[free_slot].cluster_id = g_cluster_next++;
    g_cluster[free_slot].seed_hash = seed_hash;
    g_cluster[free_slot].motif_hash = motif_hash;
    g_cluster[free_slot].confidence = 1u;
    g_stats.cluster_count++;
    return &g_cluster[free_slot];
}

void compat_bbf_init(void) {
    for (u32 i = 0; i < BBF_MAX_TASK; i++) g_task[i].used = 0;
    for (u32 i = 0; i < BBF_MAX_CLUSTER; i++) g_cluster[i].used = 0;
    g_stats.profile_count = 0;
    g_stats.cluster_count = 0;
    g_cluster_next = 1u;
}

bbf_bind_result_t compat_bbf_bind_task(u32 task_id, compat_personality_t personality, u32 seed) {
    bbf_bind_result_t out;
    out.bbf_id = hash3(seed ? seed : task_id, (u32)personality, 1u);
    out.cluster_id = 0u;
    out.confidence = 0u;

    int idx = find_task(task_id);
    if (idx < 0) idx = alloc_task();
    if (idx < 0) return out;

    bbf_task_entry_t* e = &g_task[idx];
    e->task_id = task_id;
    e->profile.valid = 1;
    e->profile.bbf_id = out.bbf_id;
    e->profile.exec_count++;
    e->profile.last_motif_hash = hash3(task_id, (u32)personality, e->profile.exec_count);

    bbf_cluster_t* c = touch_cluster(out.bbf_id, e->profile.last_motif_hash);
    if (c) {
        e->profile.cluster_id = c->cluster_id;
        e->profile.confidence = c->confidence;
        out.cluster_id = c->cluster_id;
        out.confidence = c->confidence;
    }

    g_stats.profile_count++;
    return out;
}

void compat_bbf_note_transition(u32 task_id, u16 from_nr, u16 to_nr, compat_personality_t personality) {
    int idx = find_task(task_id);
    if (idx < 0) return;
    bbf_task_entry_t* e = &g_task[idx];
    e->profile.last_motif_hash = hash3((u32)personality, from_nr * 131u, to_nr * 977u);
    bbf_cluster_t* c = touch_cluster(e->profile.bbf_id, e->profile.last_motif_hash);
    if (c) {
        e->profile.cluster_id = c->cluster_id;
        e->profile.confidence = c->confidence;
    }
}

int compat_bbf_profile(u32 task_id, bbf_profile_t* out) {
    if (!out) return -1;
    int idx = find_task(task_id);
    if (idx < 0) {
        out->valid = 0;
        return -1;
    }
    *out = g_task[idx].profile;
    return 0;
}

u32 compat_bbf_id_of(u32 task_id) {
    int idx = find_task(task_id);
    if (idx < 0) return 0u;
    return g_task[idx].profile.bbf_id;
}

void compat_bbf_stats(bbf_stats_t* out) {
    if (!out) return;
    *out = g_stats;
}

