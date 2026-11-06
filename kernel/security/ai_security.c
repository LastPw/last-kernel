#include "ai_security.h"
#include "threat_logger.h"
#include "resource_monitor.h"
#include "resource_engine.h"
#include "sdc.h"

static ai_security_status_t st;
static u32 last_threat_count = 0;

static void clamp_score(void) {
    if (st.risk_score > 1000u) {
        st.risk_score = 1000u;
    }
}

void ai_security_init(void) {
    st.risk_score = 0;
    st.predicted_threat_window = 0;
    st.anomaly_events = 0;
    st.quarantined_suggestions = 0;
    st.adaptive_firewall_level = 1;
    st.learning_epoch = 1;
    st.desktop_mode = 1;
    last_threat_count = threat_logger_count();
}

void ai_security_on_threat(u32 threat_type, u32 task_id, u32 arg0, u32 arg1) {
    (void)task_id;
    (void)arg0;
    (void)arg1;

    st.anomaly_events++;
    if (threat_type == THREAT_SIG_MATCH) {
        st.risk_score += 45;
    } else if (threat_type == THREAT_KERNEL_WRITE || threat_type == THREAT_STACK_CORRUPT) {
        st.risk_score += 60;
    } else if (threat_type == THREAT_IO_MALFORMED || threat_type == THREAT_IRQ_ABUSE) {
        st.risk_score += 20;
    } else if (threat_type == THREAT_CPU_ABUSE) {
        st.risk_score += 18;
    } else {
        st.risk_score += 10;
    }
    clamp_score();
}

void ai_security_tick(void) {
    resource_snapshot_t rs;
    sdc_status_t sdc_st;
    u32 count = threat_logger_count();
    u32 delta = 0;

    resource_monitor_snapshot(&rs);
    sdc_status(&sdc_st);

    if (count >= last_threat_count) {
        delta = count - last_threat_count;
    } else {
        delta = count;
    }
    last_threat_count = count;

    if (delta > 0) {
        st.learning_epoch++;
        st.risk_score += (delta * 6u);
        st.predicted_threat_window += delta;
    } else if (st.risk_score > 0) {
        st.risk_score--;
    }

    if (rs.cpu_busy_percent > 85u || rs.low_mem_warning) {
        if (st.risk_score > 2u) {
            st.risk_score -= 2u;
        }
    }

    if (sdc_st.mode == SDC_MODE_SERVER || rs.profile == RE_PROFILE_SERVER_FULL || rs.profile == RE_PROFILE_SERVER_LIGHT) {
        st.desktop_mode = 0;
    } else {
        st.desktop_mode = 1;
    }

    if (st.risk_score > 220u || sdc_st.network_suspicious > 8u) {
        st.adaptive_firewall_level = 3;
        st.quarantined_suggestions++;
    } else if (st.risk_score > 100u || sdc_st.network_suspicious > 2u) {
        st.adaptive_firewall_level = 2;
    } else {
        st.adaptive_firewall_level = 1;
    }

    if (st.predicted_threat_window > 500u) {
        st.predicted_threat_window = 500u;
    } else if (st.predicted_threat_window > 0u && delta == 0u) {
        st.predicted_threat_window--;
    }

    clamp_score();
}

void ai_security_status(ai_security_status_t* out) {
    if (!out) {
        return;
    }
    *out = st;
}
