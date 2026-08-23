#include "kshield.h"
#include "sdc.h"
#include "antivirus.h"
#include "ai_security.h"
#include "resource_monitor.h"
#include "scheduler.h"
#include "irq_sanitizer.h"
#include "threat_logger.h"
#include "audit.h"
#include "module.h"
#include "string.h"

#define KSHIELD_TASK_MAX 64u
#define KSHIELD_BURST_MAX 16u
#define KSHIELD_NAME_MAX 24u

typedef struct {
    u16 risk;
    u16 irq_burst;
    u8 quarantined;
    u8 throttled;
} ks_task_t;

typedef struct {
    u8 used;
    char name[KSHIELD_NAME_MAX];
} ks_blocked_t;

static ks_task_t g_task[KSHIELD_TASK_MAX];
static ks_blocked_t g_blocked[KSHIELD_BURST_MAX];
static kshield_status_t g_status;
static u32 g_last_threat_count = 0;

static void blocked_seed(void) {
    const char* deny[] = {
        "rootkit", "injector", "exploit", "ring0hack",
        "stealthdrv", "hooker", "patcher", "bypass"
    };
    for (u32 i = 0; i < KSHIELD_BURST_MAX; i++) {
        g_blocked[i].used = 0u;
        g_blocked[i].name[0] = '\0';
    }
    for (u32 i = 0; i < 8u; i++) {
        g_blocked[i].used = 1u;
        str_ncpy(g_blocked[i].name, deny[i], KSHIELD_NAME_MAX);
    }
}

static int blocked_name(const char* name) {
    if (!name || !name[0]) return 1;
    for (u32 i = 0; i < KSHIELD_BURST_MAX; i++) {
        if (!g_blocked[i].used) continue;
        if (str_cmp(g_blocked[i].name, name) == 0) return 1;
    }
    return 0;
}

static u32 clamp100(u32 v) {
    return (v > 100u) ? 100u : v;
}

static void task_update_risk(u32 task_id, u32 delta) {
    if (task_id == 0u) return;
    /* SECURITY: use modular indexing so tasks with IDs >= KSHIELD_TASK_MAX
     * are still monitored instead of silently bypassing KShield. */
    u32 slot = task_id % KSHIELD_TASK_MAX;
    if (slot == 0u) slot = 1u;
    u32 r = (u32)g_task[slot].risk + delta;
    g_task[slot].risk = (u16)(r > 1000u ? 1000u : r);
}

static void maybe_contain_task(u32 task_id) {
    if (task_id == 0u) return;
    u32 slot = task_id % KSHIELD_TASK_MAX;
    if (slot == 0u) slot = 1u;

    if (g_task[slot].risk >= 420u && !g_task[slot].quarantined) {
        (void)scheduler_set_realtime(task_id, 0);
        (void)scheduler_set_priority(task_id, 39);
        g_task[slot].quarantined = 1u;
        g_status.quarantined_tasks++;
    } else if (g_task[slot].risk >= 180u && !g_task[slot].throttled) {
        (void)scheduler_set_realtime(task_id, 0);
        (void)scheduler_set_priority(task_id, 32);
        g_task[slot].throttled = 1u;
    }
}

static void refresh_scores(void) {
    resource_snapshot_t rs;
    sdc_status_t sdc_st;
    antivirus_status_t av;
    ai_security_status_t ai;
    u32 tcount;
    u32 tdelta = 0u;
    u32 risk = 0u;
    u32 integrity = 100u;

    resource_monitor_snapshot(&rs);
    sdc_status(&sdc_st);
    antivirus_status(&av);
    ai_security_status(&ai);

    tcount = threat_logger_count();
    if (tcount >= g_last_threat_count) {
        tdelta = tcount - g_last_threat_count;
    }
    g_last_threat_count = tcount;

    risk += rs.cpu_busy_percent / 2u;
    risk += rs.low_mem_warning ? 12u : 0u;
    risk += clamp100(sdc_st.network_suspicious * 4u);
    risk += clamp100(ai.risk_score / 4u);
    risk += clamp100(av.active_threats * 18u);
    risk += clamp100(tdelta * 10u);

    if (g_status.mode == KSHIELD_MODE_HARDENED) {
        risk = clamp100((risk * 11u) / 10u);
    }
    if (g_status.lockdown_level >= 2u) {
        risk = clamp100(risk + 10u);
    }
    g_status.risk_score = risk;

    if (sdc_st.files_integrity_percent < integrity) integrity = sdc_st.files_integrity_percent;
    if (av.filesystem_integrity_percent < integrity) integrity = av.filesystem_integrity_percent;
    g_status.integrity_score = integrity;

    if (risk > 80u && g_status.lockdown_level < 2u) {
        g_status.lockdown_level = 2u;
        g_status.auto_recoveries++;
    } else if (risk < 35u && g_status.lockdown_level > 0u) {
        g_status.lockdown_level--;
    }
}

void kshield_init(void) {
    for (u32 i = 0; i < KSHIELD_TASK_MAX; i++) {
        g_task[i].risk = 0u;
        g_task[i].irq_burst = 0u;
        g_task[i].quarantined = 0u;
        g_task[i].throttled = 0u;
    }
    blocked_seed();
    g_status.enabled = 1u;
    g_status.lockdown_level = 0u;
    g_status.mode = KSHIELD_MODE_BALANCED;
    g_status.integrity_score = 100u;
    g_status.risk_score = 0u;
    g_status.blocked_module_loads = 0u;
    g_status.blocked_module_installs = 0u;
    g_status.blocked_irqs = 0u;
    g_status.blocked_io_ops = 0u;
    g_status.quarantined_tasks = 0u;
    g_status.auto_recoveries = 0u;
    g_status.policy_version = 1u;
    g_last_threat_count = threat_logger_count();
}

void kshield_tick(void) {
    if (!g_status.enabled) return;

    refresh_scores();
    for (u32 i = 1; i < KSHIELD_TASK_MAX; i++) {
        if (g_task[i].risk > 0u) g_task[i].risk--;
        if (g_task[i].irq_burst > 0u) g_task[i].irq_burst--;
    }
}

void kshield_status(kshield_status_t* out) {
    if (!out) return;
    *out = g_status;
}

void kshield_set_mode(kshield_mode_t mode) {
    g_status.mode = mode;
    g_status.policy_version++;
}

void kshield_set_lockdown(u8 level) {
    if (level > 3u) level = 3u;
    g_status.lockdown_level = level;
    g_status.policy_version++;
}

u8 kshield_lockdown(void) {
    return g_status.lockdown_level;
}

int kshield_authorize_module_load(const char* module_name) {
    module_info_t m;
    if (!g_status.enabled) return 0;
    if (!module_name || !module_name[0]) {
        g_status.blocked_module_loads++;
        return -1;
    }
    if (blocked_name(module_name)) {
        g_status.blocked_module_loads++;
        threat_logger_log(THREAT_SIG_MATCH, 0u, 0xB10C0001u, 0u);
        return -1;
    }
    if (module_get(module_name, &m) != 0) {
        g_status.blocked_module_loads++;
        return -1;
    }
    if (g_status.lockdown_level >= 2u && !m.sandboxed &&
        str_cmp(module_name, "security") != 0 &&
        str_cmp(module_name, "resource") != 0) {
        g_status.blocked_module_loads++;
        return -1;
    }
    return 0;
}

int kshield_authorize_module_install(const char* module_name) {
    if (!g_status.enabled) return 0;
    if (!module_name || !module_name[0] || blocked_name(module_name)) {
        g_status.blocked_module_installs++;
        threat_logger_log(THREAT_SIG_MATCH, 0u, 0xB10C0002u, 0u);
        return -1;
    }
    if (g_status.lockdown_level >= 3u &&
        str_cmp(module_name, "lastmap") != 0 &&
        str_cmp(module_name, "lastshark") != 0) {
        g_status.blocked_module_installs++;
        return -1;
    }
    return 0;
}

int kshield_on_irq(u8 irq, u32 global_irq_count) {
    u32 tid = scheduler_current_task();
    if (!g_status.enabled) return 0;
    /* IRQ0 drives the scheduler and watchdog time base.  Applying the
       per-task burst rule to it can deadlock the system before the periodic
       decay path gets a chance to run. Rate-limit device IRQs instead. */
    if (irq == 0u) return 0;
    if (!irq_sanitizer_allow(irq, global_irq_count)) {
        g_status.blocked_irqs++;
        task_update_risk(tid, 24u);
        maybe_contain_task(tid);
        return -1;
    }
    if (tid < KSHIELD_TASK_MAX) {
        g_task[tid].irq_burst++;
        if (g_task[tid].irq_burst > 12u) {
            g_status.blocked_irqs++;
            task_update_risk(tid, 16u);
            maybe_contain_task(tid);
            return -1;
        }
    }
    return 0;
}

int kshield_on_io(u32 task_id, u32 bytes, u32 flags) {
    (void)flags;
    if (!g_status.enabled) return 0;
    if (bytes > 16384u && g_status.lockdown_level >= 2u) {
        g_status.blocked_io_ops++;
        task_update_risk(task_id, 18u);
        maybe_contain_task(task_id);
        return -1;
    }
    if (bytes > 4096u) {
        task_update_risk(task_id, 4u);
    }
    maybe_contain_task(task_id);
    return 0;
}

void kshield_on_alloc(u32 task_id, u32 size) {
    if (!g_status.enabled) return;
    if (size > (1024u * 1024u)) task_update_risk(task_id, 28u);
    else if (size > (256u * 1024u)) task_update_risk(task_id, 9u);
    maybe_contain_task(task_id);
}

void kshield_on_threat(threat_type_t type, u32 task_id, u32 arg0, u32 arg1) {
    (void)arg0;
    (void)arg1;
    if (!g_status.enabled) return;
    if (type == THREAT_KERNEL_WRITE || type == THREAT_STACK_CORRUPT) {
        task_update_risk(task_id, 80u);
    } else if (type == THREAT_SIG_MATCH) {
        task_update_risk(task_id, 55u);
    } else if (type == THREAT_IRQ_ABUSE || type == THREAT_IO_MALFORMED) {
        task_update_risk(task_id, 30u);
    } else {
        task_update_risk(task_id, 12u);
    }
    maybe_contain_task(task_id);
    if (g_status.lockdown_level >= 2u) {
        audit_log(AUDIT_EVT_TASK_VIOLATION, task_id, (u32)type);
    }
}
