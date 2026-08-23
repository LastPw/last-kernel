#include "memory_runtime.h"
#include "hw_topology.h"

#define MEMRT_TRACK_MAX 128u
#define MEMRT_VAULT_SLOT_BYTES 128u

typedef struct {
    u8 used;
    void* ptr;
    u32 size;
    u32 task_id;
    memrt_tag_t tag;
    memrt_temp_t temp;
    u32 numa_node;
} memrt_track_t;

typedef struct {
    u8 used;
    u32 len;
    u32 key;
    u8 data[MEMRT_VAULT_SLOT_BYTES];
} memrt_vault_t;

typedef struct {
    u8 used;
    memrt_zero_copy_kind_t kind;
    void* ptr;
    u32 len;
    u32 owner_task;
} memrt_zc_t;

static memrt_track_t g_track[MEMRT_TRACK_MAX];
static memrt_vault_t g_vault[MEMRT_VAULT_MAX];
static memrt_zc_t g_zc[MEMRT_ZC_MAX];
static memrt_snapshot_t g_snap;
static u32 g_total_bytes;
static u32 g_alloc_ema;
static u32 g_last_frame_bucket;
static u32 g_secret_counter;

static u32 clamp100(u32 v) {
    return v > 100u ? 100u : v;
}

static void secure_zero(void* ptr, u32 len) {
    volatile u8* p = (volatile u8*)ptr;
    for (u32 i = 0u; i < len; i++) p[i] = 0u;
}

static u32 mix_key(u32 h) {
    h ^= h << 13;
    h ^= h >> 17;
    h ^= h << 5;
    return h ? h : 0xA5C31F27u;
}

static memrt_temp_t classify(memrt_tag_t tag, u32 size, u32 task_id) {
    if (tag == MEMRT_TAG_RENDER || tag == MEMRT_TAG_TASK || tag == MEMRT_TAG_CACHE) return MEMRT_TEMP_HOT;
    if (tag == MEMRT_TAG_NETWORK || tag == MEMRT_TAG_STORAGE || tag == MEMRT_TAG_GPU) return MEMRT_TEMP_WARM;
    if (task_id != 0u && size <= 4096u) return MEMRT_TEMP_WARM;
    return MEMRT_TEMP_COLD;
}

static u32 pick_numa_node(u32 task_id, memrt_tag_t tag) {
    u32 nodes = hw_topology_numa_nodes();
    if (nodes <= 1u) return 0u;
    if (tag == MEMRT_TAG_NETWORK || tag == MEMRT_TAG_STORAGE) return (task_id + 1u) % nodes;
    return task_id % nodes;
}

void memory_runtime_init(u32 total_bytes) {
    g_total_bytes = total_bytes;
    g_alloc_ema = 4096u;
    g_last_frame_bucket = 0u;
    g_secret_counter = 1u;
    for (u32 i = 0u; i < MEMRT_TRACK_MAX; i++) g_track[i].used = 0u;
    for (u32 i = 0u; i < MEMRT_VAULT_MAX; i++) g_vault[i].used = 0u;
    for (u32 i = 0u; i < MEMRT_ZC_MAX; i++) g_zc[i].used = 0u;
    g_snap.hot_bytes = 0u;
    g_snap.warm_bytes = 0u;
    g_snap.cold_bytes = 0u;
    g_snap.vault_bytes = 0u;
    g_snap.vault_slots = 0u;
    g_snap.zero_copy_arenas = 0u;
    g_snap.predicted_alloc = g_alloc_ema;
    g_snap.pressure_score = 0u;
    g_snap.reclaim_events = 0u;
    g_snap.compression_events = 0u;
    g_snap.migration_events = 0u;
    g_snap.anti_rowhammer_gaps = 0u;
    g_snap.fragmentation_score = 0u;
    g_snap.numa_node = 0u;
    g_snap.locality_score = 100u;
}

u32 memory_runtime_predict(u32 task_id, u32 request_bytes, memrt_tag_t tag) {
    u32 predicted = request_bytes;
    if (predicted < g_alloc_ema / 2u) predicted = g_alloc_ema / 2u;
    if (tag == MEMRT_TAG_RENDER || tag == MEMRT_TAG_CACHE) predicted += predicted / 4u;
    if (tag == MEMRT_TAG_SECRET && predicted < 64u) predicted = 64u;
    if (predicted < request_bytes) predicted = request_bytes;
    g_snap.predicted_alloc = predicted;
    g_snap.numa_node = pick_numa_node(task_id, tag);
    return predicted;
}

void memory_runtime_on_alloc(void* ptr, u32 size, u32 task_id, memrt_tag_t tag) {
    if (!ptr || size == 0u) return;
    memrt_temp_t temp = classify(tag, size, task_id);
    u32 slot = 0xFFFFFFFFu;
    for (u32 i = 0u; i < MEMRT_TRACK_MAX; i++) {
        if (!g_track[i].used) {
            slot = i;
            break;
        }
    }
    if (slot != 0xFFFFFFFFu) {
        g_track[slot].used = 1u;
        g_track[slot].ptr = ptr;
        g_track[slot].size = size;
        g_track[slot].task_id = task_id;
        g_track[slot].tag = tag;
        g_track[slot].temp = temp;
        g_track[slot].numa_node = pick_numa_node(task_id, tag);
        g_snap.numa_node = g_track[slot].numa_node;
    }
    g_alloc_ema = ((g_alloc_ema * 7u) + size) / 8u;
    if (temp == MEMRT_TEMP_HOT) g_snap.hot_bytes += size;
    else if (temp == MEMRT_TEMP_WARM) g_snap.warm_bytes += size;
    else g_snap.cold_bytes += size;

    u32 bucket = ((u32)ptr) >> 12;
    if (g_last_frame_bucket && bucket + 1u == g_last_frame_bucket) {
        g_snap.anti_rowhammer_gaps++;
    }
    g_last_frame_bucket = bucket;
}

void memory_runtime_on_free(void* ptr, u32 task_id) {
    (void)task_id;
    if (!ptr) return;
    for (u32 i = 0u; i < MEMRT_TRACK_MAX; i++) {
        if (g_track[i].used && g_track[i].ptr == ptr) {
            u32 size = g_track[i].size;
            if (g_track[i].temp == MEMRT_TEMP_HOT) {
                g_snap.hot_bytes = (g_snap.hot_bytes > size) ? (g_snap.hot_bytes - size) : 0u;
            } else if (g_track[i].temp == MEMRT_TEMP_WARM) {
                g_snap.warm_bytes = (g_snap.warm_bytes > size) ? (g_snap.warm_bytes - size) : 0u;
            } else {
                g_snap.cold_bytes = (g_snap.cold_bytes > size) ? (g_snap.cold_bytes - size) : 0u;
            }
            if (g_track[i].tag == MEMRT_TAG_SECRET) secure_zero(ptr, size < 256u ? size : 256u);
            g_track[i].used = 0u;
            return;
        }
    }
}

u32 memory_runtime_govern_pressure(u32 total_bytes, u32 used_bytes, u32 fragmentation) {
    u32 pressure = 0u;
    if (total_bytes) pressure = (used_bytes * 100u) / total_bytes;
    pressure = clamp100((pressure * 3u + fragmentation) / 4u);
    g_snap.pressure_score = pressure;
    g_snap.fragmentation_score = fragmentation;
    if (pressure >= 72u) g_snap.reclaim_events++;
    if (pressure >= 82u || fragmentation >= 65u) g_snap.compression_events++;
    if (pressure >= 88u && hw_topology_numa_nodes() > 1u) g_snap.migration_events++;
    g_snap.locality_score = 100u - (fragmentation > 80u ? 80u : fragmentation);
    return pressure;
}

int memory_runtime_vault_store(const void* data, u32 len, u32* out_handle) {
    if (!data || !out_handle || len == 0u || len > MEMRT_VAULT_SLOT_BYTES) return -1;
    for (u32 i = 0u; i < MEMRT_VAULT_MAX; i++) {
        if (!g_vault[i].used) {
            const u8* src = (const u8*)data;
            g_vault[i].used = 1u;
            g_vault[i].len = len;
            g_vault[i].key = mix_key(0xC0DEC0DEu ^ g_secret_counter++ ^ i);
            for (u32 n = 0u; n < len; n++) {
                g_vault[i].data[n] = (u8)(src[n] ^ ((g_vault[i].key >> ((n & 3u) * 8u)) & 0xFFu));
            }
            g_snap.vault_slots++;
            g_snap.vault_bytes += len;
            *out_handle = i + 1u;
            return 0;
        }
    }
    return -1;
}

int memory_runtime_vault_load(u32 handle, void* out, u32 out_len) {
    if (!handle || handle > MEMRT_VAULT_MAX || !out) return -1;
    memrt_vault_t* v = &g_vault[handle - 1u];
    if (!v->used || out_len < v->len) return -1;
    u8* dst = (u8*)out;
    for (u32 n = 0u; n < v->len; n++) {
        dst[n] = (u8)(v->data[n] ^ ((v->key >> ((n & 3u) * 8u)) & 0xFFu));
    }
    return (int)v->len;
}

int memory_runtime_vault_zeroize(u32 handle) {
    if (!handle || handle > MEMRT_VAULT_MAX) return -1;
    memrt_vault_t* v = &g_vault[handle - 1u];
    if (!v->used) return -1;
    u32 len = v->len;
    secure_zero(v->data, MEMRT_VAULT_SLOT_BYTES);
    v->used = 0u;
    v->len = 0u;
    v->key = 0u;
    if (g_snap.vault_slots) g_snap.vault_slots--;
    g_snap.vault_bytes = (g_snap.vault_bytes > len) ? (g_snap.vault_bytes - len) : 0u;
    return 0;
}

int memory_runtime_zero_copy_create(memrt_zero_copy_kind_t kind, void* ptr, u32 len, u32* out_id) {
    if (!ptr || !out_id || len == 0u) return -1;
    for (u32 i = 0u; i < MEMRT_ZC_MAX; i++) {
        if (!g_zc[i].used) {
            g_zc[i].used = 1u;
            g_zc[i].kind = kind;
            g_zc[i].ptr = ptr;
            g_zc[i].len = len;
            g_zc[i].owner_task = 0u;
            g_snap.zero_copy_arenas++;
            *out_id = i + 1u;
            return 0;
        }
    }
    return -1;
}

int memory_runtime_zero_copy_get(u32 id, memrt_zero_copy_kind_t* kind, void** ptr, u32* len) {
    if (!id || id > MEMRT_ZC_MAX || !kind || !ptr || !len) return -1;
    memrt_zc_t* z = &g_zc[id - 1u];
    if (!z->used) return -1;
    *kind = z->kind;
    *ptr = z->ptr;
    *len = z->len;
    return 0;
}

void memory_runtime_snapshot(memrt_snapshot_t* out) {
    if (out) *out = g_snap;
}
