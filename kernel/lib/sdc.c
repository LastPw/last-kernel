#include "sdc.h"
#include "resource_monitor.h"
#include "resource_engine.h"
#include "module.h"
#include "scheduler.h"
#include "cgroup.h"
#include "threat_logger.h"
#include "security_hook.h"
#include "string.h"

#define SDC_TASK_MAX 32

typedef struct {
    u16 score;
    u8 quarantined;
    u8 throttled;
} sdc_task_t;

static sdc_task_t t[SDC_TASK_MAX];
static sdc_status_t st;
static u32 quarantine_group = 0;
static u32 file_integrity_token = 0;
static u32 expected_integrity_token = 0;
static u32 packet_window = 0;
static u32 tiny_packet_window = 0;

static u32 hash32(const char* s) {
    u32 h = 2166136261u;
    if (!s) return h;
    for (u32 i = 0; s[i]; i++) {
        h ^= (u8)s[i];
        h *= 16777619u;
    }
    return h;
}

static void reset_task(u32 id) {
    if (id >= SDC_TASK_MAX) return;
    t[id].score = 0;
    t[id].quarantined = 0;
    t[id].throttled = 0;
}

static void maybe_contain(u32 id) {
    if (id == 0 || id >= SDC_TASK_MAX) {
        return;
    }
    if (t[id].score > 180 && !t[id].quarantined) {
        if (quarantine_group) {
            scheduler_set_cgroup(id, quarantine_group);
        }
        t[id].quarantined = 1;
        st.quarantined_tasks++;
        st.alerts_total++;
        threat_logger_log(THREAT_CPU_ABUSE, id, t[id].score, 1);
    } else if (t[id].score > 90 && !t[id].throttled) {
        scheduler_set_realtime(id, 0);
        scheduler_set_priority(id, 39);
        t[id].throttled = 1;
        st.throttled_tasks++;
        st.alerts_total++;
        threat_logger_log(THREAT_CPU_ABUSE, id, t[id].score, 2);
    }
}

void sdc_init(void) {
    for (u32 i = 0; i < SDC_TASK_MAX; i++) {
        reset_task(i);
    }

    st.cpu_safe = 1;
    st.ram_safe = 1;
    st.network_suspicious = 0;
    st.files_integrity_percent = 100;
    st.blocked_module_loads = 0;
    st.quarantined_tasks = 0;
    st.throttled_tasks = 0;
    st.alerts_total = 0;
    st.mode = SDC_MODE_DESKTOP;

    int gid = cgroup_create("sdc_quarantine");
    if (gid > 0) {
        quarantine_group = (u32)gid;
        cgroup_set_limits(quarantine_group, 10, 64 * 1024, 64);
    }

    expected_integrity_token = hash32("kernel/core.c") ^ hash32("kernel/kernel_entry.asm") ^ hash32("shell/commands.c");
    file_integrity_token = expected_integrity_token;
    packet_window = 0;
    tiny_packet_window = 0;
}

void sdc_set_mode(sdc_mode_t mode) {
    st.mode = mode;
}

sdc_mode_t sdc_get_mode(void) {
    return st.mode;
}

void sdc_status(sdc_status_t* out) {
    if (!out) return;
    *out = st;
}

int sdc_authorize_module_load(const char* module_name) {
    module_info_t m;
    if (!module_name || !module_name[0] || module_get(module_name, &m) != 0) {
        st.blocked_module_loads++;
        st.alerts_total++;
        return -1;
    }

    if (str_cmp(module_name, "rootkit") == 0 ||
        str_cmp(module_name, "injector") == 0 ||
        str_cmp(module_name, "exploit") == 0) {
        st.blocked_module_loads++;
        st.alerts_total++;
        threat_logger_log(THREAT_SIG_MATCH, 0, 0xBAD, 0);
        return -1;
    }

    if (!m.sandboxed && str_cmp(module_name, "resource") != 0 && str_cmp(module_name, "security") != 0) {
        st.blocked_module_loads++;
        st.alerts_total++;
        return -1;
    }
    return 0;
}

int sdc_authorize_module_install(const char* module_name) {
    if (!module_name || !module_name[0]) {
        st.blocked_module_loads++;
        st.alerts_total++;
        return -1;
    }
    if (str_cmp(module_name, "rootkit") == 0 ||
        str_cmp(module_name, "injector") == 0 ||
        str_cmp(module_name, "exploit") == 0) {
        st.blocked_module_loads++;
        st.alerts_total++;
        threat_logger_log(THREAT_SIG_MATCH, 0, 0xBAD, 1);
        return -1;
    }
    return 0;
}

void sdc_on_alloc(u32 task_id, u32 size) {
    if (task_id >= SDC_TASK_MAX) return;
    if (size > (512 * 1024u)) {
        t[task_id].score += 25;
    } else if (size > (128 * 1024u)) {
        t[task_id].score += 8;
    }
    maybe_contain(task_id);
}

void sdc_on_io(u32 task_id, u32 bytes) {
    if (task_id >= SDC_TASK_MAX) return;
    if (bytes > 8192) {
        t[task_id].score += 4;
    }
    maybe_contain(task_id);
}

void sdc_on_packet(u32 packet_len, u8 suspicious) {
    packet_window++;
    if (packet_len < 80) {
        tiny_packet_window++;
    }
    if (packet_len > 1400 || suspicious) {
        st.network_suspicious++;
        st.alerts_total++;
        if (st.mode == SDC_MODE_SERVER) {
            threat_logger_log(THREAT_IO_MALFORMED, 0, packet_len, suspicious);
        }
    }
}

void sdc_on_process_crash(u32 reason) {
    st.alerts_total++;
    threat_logger_log(THREAT_HEAP_CORRUPT, 0, reason, 0);
}

int sdc_register_safe_hook(const char* module_name, hook_fn_t fn) {
    module_info_t m;
    if (module_get(module_name, &m) != 0 || !m.loaded) {
        return -1;
    }
    if (!m.sandboxed && str_cmp(module_name, "security") != 0) {
        return -1;
    }
    return security_hook_register(module_name, fn);
}

void sdc_tick(void) {
    resource_snapshot_t rs;
    resource_monitor_snapshot(&rs);

    st.cpu_safe = (rs.cpu_busy_percent < 95) ? 1 : 0;
    st.ram_safe = (rs.low_mem_warning == 0 && rs.oom_kills < 3) ? 1 : 0;

    if (rs.profile == RE_PROFILE_SERVER_FULL || rs.profile == RE_PROFILE_SERVER_LIGHT) {
        st.mode = SDC_MODE_SERVER;
    } else {
        st.mode = SDC_MODE_DESKTOP;
    }

    if (file_integrity_token != expected_integrity_token) {
        st.files_integrity_percent = 90;
        st.alerts_total++;
        file_integrity_token = expected_integrity_token;
    } else {
        st.files_integrity_percent = 100;
    }

    if (packet_window > 300 || tiny_packet_window > 220) {
        st.network_suspicious++;
        st.alerts_total++;
        threat_logger_log(THREAT_IRQ_ABUSE, 0, packet_window, tiny_packet_window);
    }
    packet_window = 0;
    tiny_packet_window = 0;

    for (u32 i = 1; i < SDC_TASK_MAX; i++) {
        if (t[i].score > 0) {
            t[i].score--;
        }
    }
}
