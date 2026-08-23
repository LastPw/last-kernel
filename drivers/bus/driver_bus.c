#include "driver_bus.h"
#include "string.h"

typedef struct {
    u8 used;
    driver_info_t info;
    driver_ops_t ops;
} driver_slot_t;

static driver_slot_t g_drv[DRIVER_SLOT_MAX];

static void set_name(char* dst, const char* src) {
    str_ncpy(dst, src, DRIVER_NAME_MAX - 1u);
    dst[DRIVER_NAME_MAX - 1u] = '\0';
}

static int valid_name(const char* name) {
    return name && name[0];
}

static int find_slot(const char* name) {
    if (!valid_name(name)) return -1;
    for (u32 i = 0; i < DRIVER_SLOT_MAX; i++) {
        if (!g_drv[i].used) continue;
        if (str_cmp(g_drv[i].info.name, name) == 0) return (int)i;
    }
    return -1;
}

static int free_slot(void) {
    for (u32 i = 0; i < DRIVER_SLOT_MAX; i++) {
        if (!g_drv[i].used) return (int)i;
    }
    return -1;
}

static void health_recompute(driver_info_t* d) {
    if (!d) return;
    u32 h = 100u;
    if (d->failures > 0u) {
        u32 penalty = d->failures * 6u;
        h = (penalty >= 100u) ? 1u : (100u - penalty);
    }
    if (!d->active && d->starts > 0u) {
        if (h > 10u) h -= 10u;
    }
    if (d->restart_count > 8u) {
        u32 p = (d->restart_count - 8u) * 2u;
        h = (p >= h) ? 1u : (h - p);
    }
    d->health_score = h;
}

void driver_bus_init(void) {
    for (u32 i = 0; i < DRIVER_SLOT_MAX; i++) {
        g_drv[i].used = 0u;
        for (u32 j = 0; j < DRIVER_NAME_MAX; j++) g_drv[i].info.name[j] = '\0';
        g_drv[i].info.installed = 0u;
        g_drv[i].info.active = 0u;
        g_drv[i].info.sandboxed = 1u;
        g_drv[i].info.critical = 0u;
        g_drv[i].info.probe_ok = 0u;
        g_drv[i].info.starts = 0u;
        g_drv[i].info.stops = 0u;
        g_drv[i].info.failures = 0u;
        g_drv[i].info.restart_count = 0u;
        g_drv[i].info.read_ops = 0u;
        g_drv[i].info.write_ops = 0u;
        g_drv[i].info.health_score = 100u;
        g_drv[i].ops.probe = 0;
        g_drv[i].ops.start = 0;
        g_drv[i].ops.stop = 0;
        g_drv[i].ops.read = 0;
        g_drv[i].ops.write = 0;
        g_drv[i].ops.ioctl = 0;
    }
}

int driver_register(const char* name, const driver_ops_t* ops) {
    if (!valid_name(name)) return -1;
    if (find_slot(name) >= 0) return 0;

    int s = free_slot();
    if (s < 0) return -1;

    g_drv[s].used = 1u;
    g_drv[s].info.installed = 1u;
    g_drv[s].info.active = 0u;
    g_drv[s].info.sandboxed = 1u;
    g_drv[s].info.critical = 0u;
    g_drv[s].info.probe_ok = (ops && ops->probe) ? 0u : 1u;
    g_drv[s].info.starts = 0u;
    g_drv[s].info.stops = 0u;
    g_drv[s].info.failures = 0u;
    g_drv[s].info.restart_count = 0u;
    g_drv[s].info.read_ops = 0u;
    g_drv[s].info.write_ops = 0u;
    g_drv[s].info.health_score = 100u;
    set_name(g_drv[s].info.name, name);
    if (ops) g_drv[s].ops = *ops;
    return 0;
}

int driver_unregister(const char* name) {
    int s = find_slot(name);
    if (s < 0) return -1;
    if (g_drv[s].info.active || g_drv[s].info.critical) return -1;

    g_drv[s].used = 0u;
    g_drv[s].info.installed = 0u;
    g_drv[s].info.active = 0u;
    g_drv[s].info.sandboxed = 0u;
    g_drv[s].info.critical = 0u;
    g_drv[s].info.probe_ok = 0u;
    g_drv[s].info.starts = 0u;
    g_drv[s].info.stops = 0u;
    g_drv[s].info.failures = 0u;
    g_drv[s].info.restart_count = 0u;
    g_drv[s].info.read_ops = 0u;
    g_drv[s].info.write_ops = 0u;
    g_drv[s].info.health_score = 0u;
    for (u32 j = 0; j < DRIVER_NAME_MAX; j++) g_drv[s].info.name[j] = '\0';
    return 0;
}

int driver_start(const char* name) {
    int s = find_slot(name);
    if (s < 0) return -1;
    if (g_drv[s].info.active) return 0;
    if (!g_drv[s].info.probe_ok) return -1;

    if (g_drv[s].ops.start) {
        if (g_drv[s].ops.start() != 0) {
            g_drv[s].info.failures++;
            health_recompute(&g_drv[s].info);
            return -1;
        }
    }
    g_drv[s].info.active = 1u;
    g_drv[s].info.starts++;
    health_recompute(&g_drv[s].info);
    return 0;
}

int driver_stop(const char* name) {
    int s = find_slot(name);
    if (s < 0) return -1;
    if (!g_drv[s].info.active) return 0;
    if (g_drv[s].info.critical) return -1;

    if (g_drv[s].ops.stop) {
        if (g_drv[s].ops.stop() != 0) {
            g_drv[s].info.failures++;
            health_recompute(&g_drv[s].info);
            return -1;
        }
    }
    g_drv[s].info.active = 0u;
    g_drv[s].info.stops++;
    health_recompute(&g_drv[s].info);
    return 0;
}

int driver_ioctl(const char* name, u32 cmd, u32 arg) {
    int s = find_slot(name);
    if (s < 0) return -1;
    if (!g_drv[s].info.active) return -1;
    if (!g_drv[s].ops.ioctl) return -1;
    return g_drv[s].ops.ioctl(cmd, arg);
}

int driver_read(const char* name, u8* out, u32 len) {
    int s = find_slot(name);
    if (s < 0 || !out || len == 0u) return -1;
    if (!g_drv[s].info.active || !g_drv[s].ops.read) return -1;
    int rc = g_drv[s].ops.read(out, len);
    if (rc >= 0) g_drv[s].info.read_ops++;
    else g_drv[s].info.failures++;
    health_recompute(&g_drv[s].info);
    return rc;
}

int driver_write(const char* name, const u8* in, u32 len) {
    int s = find_slot(name);
    if (s < 0 || !in || len == 0u) return -1;
    if (!g_drv[s].info.active || !g_drv[s].ops.write) return -1;
    int rc = g_drv[s].ops.write(in, len);
    if (rc >= 0) g_drv[s].info.write_ops++;
    else g_drv[s].info.failures++;
    health_recompute(&g_drv[s].info);
    return rc;
}

int driver_recover(const char* name) {
    int s = find_slot(name);
    if (s < 0) return -1;
    if (g_drv[s].info.critical && g_drv[s].info.active) return 0;

    if (g_drv[s].info.active) {
        (void)driver_stop(name);
    }
    if (g_drv[s].ops.probe && g_drv[s].ops.probe() != 0) {
        g_drv[s].info.probe_ok = 0u;
        g_drv[s].info.failures++;
        health_recompute(&g_drv[s].info);
        return -1;
    }
    g_drv[s].info.probe_ok = 1u;
    int rc = driver_start(name);
    if (rc == 0) {
        g_drv[s].info.restart_count++;
    }
    health_recompute(&g_drv[s].info);
    return rc;
}

int driver_set_flags(const char* name, u8 sandboxed, u8 critical) {
    int s = find_slot(name);
    if (s < 0) return -1;
    g_drv[s].info.sandboxed = sandboxed ? 1u : 0u;
    g_drv[s].info.critical = critical ? 1u : 0u;
    return 0;
}

int driver_probe_all(void) {
    int ok = 0;
    for (u32 i = 0; i < DRIVER_SLOT_MAX; i++) {
        if (!g_drv[i].used) continue;
        if (!g_drv[i].ops.probe) {
            g_drv[i].info.probe_ok = 1u;
            ok++;
            continue;
        }
        if (g_drv[i].ops.probe() == 0) {
            g_drv[i].info.probe_ok = 1u;
            ok++;
        } else {
            g_drv[i].info.probe_ok = 0u;
            g_drv[i].info.failures++;
        }
        health_recompute(&g_drv[i].info);
    }
    return ok;
}

u32 driver_count(void) {
    u32 n = 0u;
    for (u32 i = 0; i < DRIVER_SLOT_MAX; i++) {
        if (g_drv[i].used) n++;
    }
    return n;
}

int driver_info_by_index(u32 idx, driver_info_t* out) {
    if (!out) return -1;
    u32 seen = 0u;
    for (u32 i = 0; i < DRIVER_SLOT_MAX; i++) {
        if (!g_drv[i].used) continue;
        if (seen == idx) {
            *out = g_drv[i].info;
            return 0;
        }
        seen++;
    }
    return -1;
}

void driver_bus_stats(driver_bus_stats_t* out) {
    if (!out) return;
    out->installed = 0u;
    out->active = 0u;
    out->total_failures = 0u;
    out->total_restarts = 0u;
    out->avg_health = 0u;
    for (u32 i = 0; i < DRIVER_SLOT_MAX; i++) {
        if (!g_drv[i].used) continue;
        out->installed++;
        if (g_drv[i].info.active) out->active++;
        out->total_failures += g_drv[i].info.failures;
        out->total_restarts += g_drv[i].info.restart_count;
        out->avg_health += g_drv[i].info.health_score;
    }
    if (out->installed > 0u) out->avg_health /= out->installed;
}
