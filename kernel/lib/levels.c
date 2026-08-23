#include "levels.h"
#include "kinit.h"
#include "paging.h"
#include "frame_allocator.h"
#include "scheduler.h"
#include "vfs.h"
#include "netstack.h"
#include "kshield.h"
#include "resource_monitor.h"
#include "timer.h"
#include "vm.h"

typedef struct {
    u8 target_level;
    u8 current_level;
    u16 reserved;
    u32 met_gates;
    u32 score_percent;
    u32 blockers;
    u32 last_tick;
} levels_ctx_t;

static levels_ctx_t g;

static const u32 g_level_req[LEVEL_COUNT] = {
    LEVEL_GATE_BOOT,
    LEVEL_GATE_BOOT | LEVEL_GATE_MM | LEVEL_GATE_VM_COW | LEVEL_GATE_SCHED,
    LEVEL_GATE_BOOT | LEVEL_GATE_MM | LEVEL_GATE_VM_COW | LEVEL_GATE_SCHED | LEVEL_GATE_VFS,
    LEVEL_GATE_BOOT | LEVEL_GATE_MM | LEVEL_GATE_VM_COW | LEVEL_GATE_SCHED | LEVEL_GATE_VFS | LEVEL_GATE_NET,
    LEVEL_GATE_BOOT | LEVEL_GATE_MM | LEVEL_GATE_VM_COW | LEVEL_GATE_SCHED | LEVEL_GATE_VFS | LEVEL_GATE_NET | LEVEL_GATE_SEC,
    LEVEL_GATE_BOOT | LEVEL_GATE_MM | LEVEL_GATE_VM_COW | LEVEL_GATE_SCHED | LEVEL_GATE_VFS | LEVEL_GATE_NET | LEVEL_GATE_SEC,
    LEVEL_GATE_BOOT | LEVEL_GATE_MM | LEVEL_GATE_VM_COW | LEVEL_GATE_SCHED | LEVEL_GATE_VFS | LEVEL_GATE_NET | LEVEL_GATE_SEC | LEVEL_GATE_PERF
};

static u32 compute_gates(void) {
    u32 gates = 0u;

    kinit_status_t ks;
    ks.total = 0u;
    ks.done = 0u;
    ks.failed = 0u;
    kinit_status(&ks);
    if (ks.total > 0u && ks.failed == 0u && ks.done == ks.total) {
        gates |= LEVEL_GATE_BOOT;
    }

    if (frame_total() > 0u && paging_wx_enforced()) {
        gates |= LEVEL_GATE_MM;
    }

    {
        vm_stats_t vs;
        vm_stats(&vs);
        if (vs.cow_maps > 0u || vs.cow_splits > 0u || (vs.active_regions > 0u && paging_wx_enforced())) {
            gates |= LEVEL_GATE_VM_COW;
        }
    }

    if (scheduler_task_count() > 0u) {
        gates |= LEVEL_GATE_SCHED;
    }

    {
        vfs_stats_t vs;
        vfs_stats(&vs);
        if (vs.active_nodes > 0u) {
            gates |= LEVEL_GATE_VFS;
        }
    }

    {
        netstack_status_t ns;
        netstack_status(&ns);
        if (ns.route_entries > 0u || ns.local_ip != 0u) {
            gates |= LEVEL_GATE_NET;
        }
    }

    {
        kshield_status_t ss;
        kshield_status(&ss);
        if (ss.enabled && ss.integrity_score >= 50u) {
            gates |= LEVEL_GATE_SEC;
        }
    }

    {
        resource_snapshot_t rs;
        resource_monitor_snapshot(&rs);
        if (rs.cpu_busy_percent <= 95u && rs.oom_kills < 3u) {
            gates |= LEVEL_GATE_PERF;
        }
    }

    return gates;
}

static u32 popcount_u32(u32 v) {
    u32 c = 0u;
    while (v) {
        v &= (v - 1u);
        c++;
    }
    return c;
}

static u8 detect_current_level(u32 gates) {
    u8 level = 0u;
    for (u8 i = 0u; i < LEVEL_COUNT; i++) {
        if ((gates & g_level_req[i]) == g_level_req[i]) {
            level = (u8)(i + 1u);
        } else {
            break;
        }
    }
    return level;
}

void levels_init(void) {
    g.target_level = 1u;
    g.current_level = 0u;
    g.met_gates = 0u;
    g.score_percent = 0u;
    g.blockers = 0u;
    g.last_tick = 0u;
}

void levels_tick(void) {
    u32 gates = compute_gates();
    u32 required = g_level_req[LEVEL_COUNT - 1u];
    u32 met = gates & required;
    u32 met_count = popcount_u32(met);
    u32 req_count = popcount_u32(required);

    g.met_gates = gates;
    g.current_level = detect_current_level(gates);
    g.score_percent = (req_count == 0u) ? 0u : ((met_count * 100u) / req_count);
    g.blockers = required & ~gates;
    g.last_tick = timer_ticks();
}

int levels_set_target(u8 level_id) {
    if (level_id < 1u || level_id > LEVEL_COUNT) {
        return -1;
    }
    g.target_level = level_id;
    return 0;
}

void levels_status(level_status_t* out) {
    if (!out) return;
    levels_tick();
    out->target_level = g.target_level;
    out->current_level = g.current_level;
    out->reserved = 0u;
    out->met_gates = g.met_gates;
    out->score_percent = g.score_percent;
    out->blockers = g.blockers;
    out->tick = g.last_tick;
}

int levels_get_info(u8 level_id, level_info_t* out) {
    if (!out) return -1;
    if (level_id < 1u || level_id > LEVEL_COUNT) return -1;

    levels_tick();
    u32 req = g_level_req[level_id - 1u];
    u32 met = g.met_gates & req;
    u32 req_count = popcount_u32(req);
    u32 met_count = popcount_u32(met);

    out->level_id = level_id;
    out->passed = ((met & req) == req) ? 1u : 0u;
    out->reserved = 0u;
    out->required_gates = req;
    out->met_gates = met;
    out->readiness_percent = (req_count == 0u) ? 0u : ((met_count * 100u) / req_count);
    return 0;
}
