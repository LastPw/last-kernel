#include "antivirus.h"
#include "signature_updater.h"
#include "io_guard.h"
#include "kernel_monitor.h"
#include "ai_security.h"
#include "threat_logger.h"
#include "process_monitor.h"
#include "resource_monitor.h"
#include "resource_engine.h"
#include "scheduler.h"
#include "cgroup.h"
#include "sdc.h"
#include "pcoe2.h"
#include "timer.h"
#include "vm.h"
#include "string.h"

#define SIG_COUNT 6
#define SIG_MAX_LEN 12
#define AV_TASK_MAX 32

typedef struct {
    u16 risk;
    u8 quarantined;
    u8 heat;
    u32 last_runtime;
    u32 last_scan_tick;
} av_task_state_t;

static u8 signatures[SIG_COUNT][SIG_MAX_LEN] = {
    { 'B','A','D','C','O','D','E',0 },
    { 'M','A','L','W','A','R','E',0 },
    { 'R','O','O','T','K','I','T',0 },
    { 'E','X','P','L','O','I','T',0 },
    { 'I','N','J','E','C','T',0 },
    { 'B','A','C','K','D','O','O','R',0 }
};

static antivirus_status_t st;
static av_task_state_t tasks[AV_TASK_MAX];
static u32 quarantine_group = 0;
static u32 file_integrity = 100;
static u32 file_token = 0;
static u32 expected_file_token = 0;
static u32 tick_div = 0;

static u32 hash_name(const char* s) {
    u32 h = 2166136261u;
    u32 i = 0;
    if (!s) {
        return h;
    }
    while (s[i]) {
        h ^= (u8)s[i];
        h *= 16777619u;
        i++;
    }
    return h;
}

static u16 sat_add16(u16 v, u16 inc) {
    u32 t = (u32)v + (u32)inc;
    return (t > 0xFFFFu) ? 0xFFFFu : (u16)t;
}

static u8 sat_add8(u8 v, u8 inc) {
    u16 t = (u16)v + (u16)inc;
    return (t > 0xFFu) ? 0xFFu : (u8)t;
}

static int match_sig(const u8* data, u32 len, const u8* sig) {
    u32 slen = 0;
    u32 i;
    while (slen < SIG_MAX_LEN && sig[slen]) {
        slen++;
    }

    if (slen == 0 || len < slen) {
        return 0;
    }

    for (i = 0; i + slen <= len; i++) {
        u32 j = 0;
        while (j < slen && data[i + j] == sig[j]) {
            j++;
        }
        if (j == slen) {
            return 1;
        }
    }

    return 0;
}

static void quarantine_task(u32 task_id, u32 reason) {
    if (task_id >= AV_TASK_MAX || task_id == 0) {
        return;
    }
    if (tasks[task_id].quarantined) {
        return;
    }

    if (quarantine_group) {
        scheduler_set_cgroup(task_id, quarantine_group);
    }
    scheduler_set_realtime(task_id, 0);
    scheduler_set_priority(task_id, 39);
    tasks[task_id].quarantined = 1;
    tasks[task_id].heat = 255u;
    st.quarantined_processes++;
    threat_logger_log(THREAT_CPU_ABUSE, task_id, reason, 0xA11);
    ai_security_on_threat(THREAT_CPU_ABUSE, task_id, reason, 0xA11);
}

void antivirus_init(void) {
    u32 i;
    st.active_threats = 0;
    st.quarantined_processes = 0;
    st.network_anomalies = 0;
    st.filesystem_integrity_percent = 100;
    st.signature_version = 1;
    st.heuristic_version = 1;
    st.scans_total = 0;
    st.infected_events = 0;

    for (i = 0; i < AV_TASK_MAX; i++) {
        tasks[i].risk = 0;
        tasks[i].quarantined = 0;
        tasks[i].heat = 0;
        tasks[i].last_runtime = 0;
        tasks[i].last_scan_tick = 0;
    }

    quarantine_group = 0;
    {
        int gid = cgroup_create("av_quarantine");
        if (gid > 0) {
            quarantine_group = (u32)gid;
            cgroup_set_limits(quarantine_group, 5, 32 * 1024, 32);
        }
    }

    file_token = hash_name("kernel/core.c") ^ hash_name("kernel/kernel_entry.asm") ^ hash_name("shell/commands.c");
    expected_file_token = file_token;
    file_integrity = 100;
    tick_div = 0;
}

void antivirus_tick(void) {
    resource_snapshot_t rs;
    sdc_status_t s;
    vm_stats_t vs;
    u32 i;
    u32 inspect_budget;

    tick_div++;
    if ((tick_div % 10u) != 0u) {
        return;
    }

    resource_monitor_snapshot(&rs);
    sdc_status(&s);
    vm_stats(&vs);

    inspect_budget = (s.mode == SDC_MODE_SERVER) ? 24u : 8u;
    inspect_budget = pcoe2_security_scan_budget(inspect_budget, rs.cpu_busy_percent);
    if (vs.protection_faults > 4u || vs.oom_faults > 2u) {
        inspect_budget += 4u;
    }
    if (inspect_budget == 0u) {
        inspect_budget = 1u;
    }

    for (i = 1; i < inspect_budget && i < AV_TASK_MAX; i++) {
        process_info_t p;
        u32 runtime_delta = 0;
        u32 risk_threshold = pcoe2_task_risk_threshold();
        if (process_monitor_snapshot(i, &p) != 0) {
            continue;
        }
        runtime_delta = (p.runtime_ticks > tasks[i].last_runtime) ? (p.runtime_ticks - tasks[i].last_runtime) : 0u;
        tasks[i].last_runtime = p.runtime_ticks;
        tasks[i].last_scan_tick = timer_ticks();

        if (p.suspicious_score > 28u) {
            tasks[i].risk = sat_add16(tasks[i].risk, 8u);
            tasks[i].heat = sat_add8(tasks[i].heat, 2u);
            ai_security_on_threat(THREAT_CPU_ABUSE, i, p.suspicious_score, p.runtime_ticks);
        } else if (tasks[i].risk > 0) {
            u32 decay = (pcoe2_performance_pressure() > 70u) ? 1u : 2u;
            tasks[i].risk = (tasks[i].risk > decay) ? (tasks[i].risk - decay) : 0u;
            if (tasks[i].heat > 0u) tasks[i].heat--;
        }

        if (p.blocked) {
            tasks[i].risk = sat_add16(tasks[i].risk, 16u);
            tasks[i].heat = sat_add8(tasks[i].heat, 3u);
        }

        if (runtime_delta > 300u && p.suspicious_score > 18u) {
            tasks[i].risk = sat_add16(tasks[i].risk, 6u);
            tasks[i].heat = sat_add8(tasks[i].heat, 2u);
        }
        if (vs.protection_faults > 0u && runtime_delta > 120u) {
            tasks[i].risk = sat_add16(tasks[i].risk, 4u);
        }
        if (vs.oom_faults > 0u && p.suspicious_score > 12u) {
            tasks[i].risk = sat_add16(tasks[i].risk, 3u);
        }

        if (tasks[i].heat > 180u && tasks[i].risk < risk_threshold) {
            tasks[i].risk = (u16)risk_threshold;
        }

        if (tasks[i].risk > risk_threshold) {
            quarantine_task(i, tasks[i].risk);
            st.active_threats++;
        }
    }

    if (file_token != expected_file_token) {
        file_integrity = 92;
        st.active_threats++;
        threat_logger_log(THREAT_SIG_MATCH, 0, 0xF11E, file_token);
        ai_security_on_threat(THREAT_SIG_MATCH, 0, 0xF11E, file_token);
        file_token = expected_file_token;
    } else if (file_integrity < 100) {
        file_integrity++;
    }

    st.filesystem_integrity_percent = file_integrity;
}

int antivirus_scan_buffer(const u8* data, u32 len, u32 task_id) {
    u32 i;
    st.scans_total++;
    if (!data || len == 0) {
        return 0;
    }

    for (i = 0; i < SIG_COUNT; i++) {
        if (match_sig(data, len, signatures[i])) {
            st.infected_events++;
            st.active_threats++;
            threat_logger_log(THREAT_SIG_MATCH, task_id, i, len);
            ai_security_on_threat(THREAT_SIG_MATCH, task_id, i, len);
            if (task_id < AV_TASK_MAX) {
                tasks[task_id].risk = sat_add16(tasks[task_id].risk, 24u);
                tasks[task_id].heat = sat_add8(tasks[task_id].heat, 16u);
                if (tasks[task_id].risk > 32) {
                    quarantine_task(task_id, i);
                }
            }
            return -1;
        }
    }

    return 0;
}

void antivirus_on_file_event(const char* name, const u8* data, u32 len, u32 task_id) {
    u32 local = hash_name(name);
    if (name && (str_cmp(name, "kernel/core.c") == 0 || str_cmp(name, "kernel/kernel_entry.asm") == 0)) {
        expected_file_token ^= local;
        expected_file_token ^= (len + task_id);
    }
    if (data) {
        (void)antivirus_scan_buffer(data, len, task_id);
    }
}

void antivirus_on_network_event(u32 packet_len, u8 suspicious) {
    if (packet_len > 1400u || suspicious) {
        st.network_anomalies++;
        st.active_threats++;
        threat_logger_log(THREAT_IO_MALFORMED, 0, packet_len, suspicious);
        ai_security_on_threat(THREAT_IO_MALFORMED, 0, packet_len, suspicious);
    }
}

void antivirus_status(antivirus_status_t* out) {
    if (!out) {
        return;
    }
    *out = st;
}

/* --- In-Kernel Threat Logger --- */
#include "spinlock.h"
#include "kshield.h"

static threat_record_t s_threat_ring[THREAT_LOG_CAP] __attribute__((aligned(64)));
static u32 s_threat_head = 0;
static u32 s_threat_next_id = 1;
static spinlock_t s_threat_lock;

void threat_logger_init(void) {
    spinlock_init(&s_threat_lock);
    u32 irqf = spinlock_lock_irqsave(&s_threat_lock);
    s_threat_head = 0;
    s_threat_next_id = 1;
    for (u32 i = 0; i < THREAT_LOG_CAP; i++) {
        s_threat_ring[i].id = 0;
        s_threat_ring[i].tick = 0;
        s_threat_ring[i].task_id = 0;
        s_threat_ring[i].type = THREAT_NONE;
        s_threat_ring[i].arg0 = 0;
        s_threat_ring[i].arg1 = 0;
    }
    spinlock_unlock_irqrestore(&s_threat_lock, irqf);
}

void threat_logger_log(threat_type_t type, u32 task_id, u32 arg0, u32 arg1) {
    u32 irqf = spinlock_lock_irqsave(&s_threat_lock);
    u32 idx = s_threat_head % THREAT_LOG_CAP;
    s_threat_ring[idx].id = s_threat_next_id++;
    s_threat_ring[idx].tick = timer_ticks();
    s_threat_ring[idx].task_id = task_id;
    s_threat_ring[idx].type = (u32)type;
    s_threat_ring[idx].arg0 = arg0;
    s_threat_ring[idx].arg1 = arg1;
    s_threat_head++;
    spinlock_unlock_irqrestore(&s_threat_lock, irqf);
    kshield_on_threat(type, task_id, arg0, arg1);
}

u32 threat_logger_count(void) {
    u32 irqf = spinlock_lock_irqsave(&s_threat_lock);
    u32 h = s_threat_head;
    spinlock_unlock_irqrestore(&s_threat_lock, irqf);
    return h;
}

int threat_logger_get(u32 reverse_index, threat_record_t* out) {
    u32 irqf = spinlock_lock_irqsave(&s_threat_lock);
    if (!out) {
        spinlock_unlock_irqrestore(&s_threat_lock, irqf);
        return -1;
    }
    if (s_threat_head == 0 || reverse_index >= THREAT_LOG_CAP || reverse_index >= s_threat_head) {
        spinlock_unlock_irqrestore(&s_threat_lock, irqf);
        return -1;
    }
    u32 pos = (s_threat_head - 1 - reverse_index) % THREAT_LOG_CAP;
    if (s_threat_ring[pos].id == 0) {
        spinlock_unlock_irqrestore(&s_threat_lock, irqf);
        return -1;
    }
    *out = s_threat_ring[pos];
    spinlock_unlock_irqrestore(&s_threat_lock, irqf);
    return 0;
}

/* --- In-Kernel Signature Updater --- */
#define SIG_BLOB_MAX 256
static u8 s_sig_blob[SIG_BLOB_MAX];

void signature_updater_init(void) {
    for (u32 i = 0; i < SIG_BLOB_MAX; i++) s_sig_blob[i] = 0;
}

int signature_updater_apply(const sig_blob_t* blob) {
    if (!blob || !blob->data || blob->size > SIG_BLOB_MAX) return -1;
    return 0;
}

/* --- In-Kernel IO Guard --- */
void io_guard_init(void) {}
void io_guard_on_irq(u8 irq, u32 irq_count) {
    if (irq > 15) threat_logger_log(THREAT_IRQ_ABUSE, 0, irq, irq_count);
}
int io_guard_filter_input(int ch) {
    if (ch == '\n' || ch == '\b' || ch == '\t' || ch == 3 || ch == 20 || ch == 22) return ch;
    if (ch == 0x81 || ch == 0x82 || ch == 0x83 || ch == 0x84) return ch;
    if (ch < 32 || ch > 126) {
        threat_logger_log(THREAT_IO_MALFORMED, 0, (u32)ch, 0);
        return -1;
    }
    return ch;
}

/* --- In-Kernel Monitor --- */
#include "security.h"
#include "audit.h"

static u32 s_security_score = 100;
void kernel_monitor_init(void) { s_security_score = 100; }
void kernel_monitor_tick(void) {
    u32 v = audit_violation_count();
    s_security_score = (v > 50) ? 50 : (100 - v);
}
u32 kernel_monitor_security_score(void) { return s_security_score; }
