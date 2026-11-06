#include "resource_engine.h"

typedef struct {
    u32 mem_ema;
    u32 io_ema;
    u32 net_ema;
    u8 used;
} re_task_stat_t;

static re_task_stat_t tstat[RE_TASK_MAX];
static resource_engine_snapshot_t snap;
static u32 total_ram = 0;
static u32 core_count = 1;
static u8 modern = 0;
static u32 predicted_cache = 4096;
static u32 thermal_x10 = 420;
static power_mode_t rec_mode = POWER_MODE_AUTO;
static re_profile_t profile_mode = RE_PROFILE_AUTO;

static void ema_u32(u32* v, u32 sample, u32 weight_old, u32 weight_new) {
    *v = ((*v * weight_old) + (sample * weight_new)) / (weight_old + weight_new);
}

void resource_engine_init(u32 total_ram_bytes, u32 cores, u8 modern_profile) {
    total_ram = total_ram_bytes;
    core_count = (cores == 0) ? 1 : cores;
    modern = modern_profile;
    predicted_cache = 4096;
    thermal_x10 = modern ? 400 : 450;
    rec_mode = POWER_MODE_AUTO;
    profile_mode = RE_PROFILE_AUTO;
    for (u32 i = 0; i < RE_TASK_MAX; i++) {
        tstat[i].used = 0;
        tstat[i].mem_ema = 0;
        tstat[i].io_ema = 0;
        tstat[i].net_ema = 0;
    }
}

void resource_engine_set_profile(re_profile_t profile) {
    profile_mode = profile;
}

re_profile_t resource_engine_get_profile(void) {
    return profile_mode;
}

u32 resource_engine_predict_alloc(u32 task_id, u32 request_bytes, u32 used_bytes, u32 total_bytes, u32 sandbox_limit_bytes) {
    u32 idx = task_id % RE_TASK_MAX;
    if (!tstat[idx].used) {
        tstat[idx].used = 1;
        tstat[idx].mem_ema = request_bytes;
    }

    u32 predict = tstat[idx].mem_ema + (request_bytes / 2u);
    if (predict < request_bytes) {
        predict = request_bytes;
    }
    predicted_cache = predict;

    u32 reserve = total_bytes / 20u;
    if (profile_mode == RE_PROFILE_SERVER_FULL || profile_mode == RE_PROFILE_SERVER_LIGHT) {
        reserve = total_bytes / 10u;
    } else if (profile_mode == RE_PROFILE_DESKTOP_LIGHT || profile_mode == RE_PROFILE_DESKTOP_FULL) {
        reserve = total_bytes / 25u;
    }

    if (used_bytes + predict + reserve > total_bytes) {
        snap.low_mem_warning = 1;
        if (used_bytes + request_bytes + reserve > total_bytes) {
            return 0;
        }
        return request_bytes;
    }

    if (sandbox_limit_bytes && predict > sandbox_limit_bytes) {
        return 0;
    }

    snap.low_mem_warning = 0;
    return predict;
}

void resource_engine_note_alloc(u32 task_id, u32 size) {
    u32 idx = task_id % RE_TASK_MAX;
    if (!tstat[idx].used) {
        tstat[idx].used = 1;
    }
    if (tstat[idx].mem_ema == 0) {
        tstat[idx].mem_ema = size;
    } else {
        ema_u32(&tstat[idx].mem_ema, size, 7, 1);
    }
}

void resource_engine_note_free(u32 task_id, u32 size) {
    u32 idx = task_id % RE_TASK_MAX;
    if (!tstat[idx].used || tstat[idx].mem_ema == 0) {
        return;
    }
    if (size >= tstat[idx].mem_ema) {
        tstat[idx].mem_ema /= 2;
    } else {
        tstat[idx].mem_ema -= size;
    }
}

void resource_engine_note_io(u32 task_id, u32 bytes, int is_net) {
    u32 idx = task_id % RE_TASK_MAX;
    if (!tstat[idx].used) {
        tstat[idx].used = 1;
    }
    ema_u32(&tstat[idx].io_ema, bytes, 7, 1);
    if (is_net) {
        ema_u32(&tstat[idx].net_ema, bytes, 7, 1);
    }
}

void resource_engine_tick(u32 cpu_busy_percent, u32 mem_used_bytes, u32 mem_total_bytes, u32 cores) {
    core_count = cores ? cores : core_count;
    total_ram = mem_total_bytes ? mem_total_bytes : total_ram;

    u32 mem_percent = 0;
    if (mem_total_bytes) {
        mem_percent = (mem_used_bytes * 100u) / mem_total_bytes;
    }
    snap.overcommit_permille = mem_total_bytes ? ((mem_used_bytes * 1000u) / mem_total_bytes) : 0;

    if (cpu_busy_percent > 80 || mem_percent > 85) {
        thermal_x10 += modern ? 3 : 5;
    } else if (thermal_x10 > 350) {
        thermal_x10 -= 2;
    }
    if (thermal_x10 > 980) {
        thermal_x10 = 980;
    }

    if (thermal_x10 > 850 || mem_percent > 90) {
        rec_mode = POWER_MODE_LOW_POWER;
    } else if (cpu_busy_percent > 70 && thermal_x10 < 760) {
        rec_mode = POWER_MODE_PERFORMANCE;
    } else {
        rec_mode = POWER_MODE_BALANCED;
    }

    if ((profile_mode == RE_PROFILE_SERVER_FULL || profile_mode == RE_PROFILE_SERVER_LIGHT) &&
        thermal_x10 < 900 && mem_percent < 92) {
        rec_mode = POWER_MODE_PERFORMANCE;
    }
    if ((profile_mode == RE_PROFILE_DESKTOP_LIGHT || profile_mode == RE_PROFILE_DESKTOP_FULL) &&
        thermal_x10 > 780) {
        rec_mode = POWER_MODE_LOW_POWER;
    }

    if (profile_mode == RE_PROFILE_DESKTOP_LIGHT) {
        rec_mode = POWER_MODE_LOW_POWER;
    }
    if (profile_mode == RE_PROFILE_DESKTOP_FULL) {
        if (mem_percent < 90) {
            rec_mode = POWER_MODE_BALANCED;
        }
    }
    if (profile_mode == RE_PROFILE_SERVER_LIGHT) {
        if (thermal_x10 < 840) {
            rec_mode = POWER_MODE_BALANCED;
        }
    }
    if (profile_mode == RE_PROFILE_SERVER_FULL) {
        if (thermal_x10 < 900 && mem_percent < 95) {
            rec_mode = POWER_MODE_PERFORMANCE;
        }
    }
}

power_mode_t resource_engine_recommend_power_mode(void) {
    return rec_mode;
}

u32 resource_engine_recommend_threads(u32 workload_score, u32 cores, u8 realtime) {
    if (cores == 0) {
        cores = core_count;
    }
    if (cores == 0) {
        cores = 1;
    }
    u32 base = 1 + (workload_score / 25u);
    if (base > cores) {
        base = cores;
    }
    if (realtime && base < cores) {
        base++;
    }
    if (snap.thermal_celsius > 84 && base > 1) {
        base--;
    }
    if ((profile_mode == RE_PROFILE_SERVER_FULL || profile_mode == RE_PROFILE_SERVER_LIGHT) && base < cores) {
        base++;
    }
    if (profile_mode == RE_PROFILE_DESKTOP_LIGHT && base > 2) {
        base = 2;
    }
    if (profile_mode == RE_PROFILE_SERVER_FULL && base < cores) {
        base++;
    }
    if (base > cores) {
        base = cores;
    }
    return base;
}

void resource_engine_snapshot(resource_engine_snapshot_t* out) {
    snap.thermal_celsius = thermal_x10 / 10u;
    snap.predicted_next_alloc = predicted_cache;
    snap.recommended_threads = resource_engine_recommend_threads(70, core_count, 0);
    snap.recommended_mode = rec_mode;
    snap.profile = profile_mode;
    if (out) {
        *out = snap;
    }
}
