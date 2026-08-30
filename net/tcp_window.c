#include "tcp_window.h"

u32 tcp_eff_window(u32 cwnd_bytes, u32 peer_wnd_bytes) {
    u32 eff = cwnd_bytes;
    if (peer_wnd_bytes < eff) eff = peer_wnd_bytes;
    return eff;
}

int tcp_can_send(u32 unacked_bytes, u32 len, u32 eff_window) {
    if (len == 0u) return 0;
    if (unacked_bytes > eff_window) return 0;
    return (eff_window - unacked_bytes) >= len;
}

/* Advertised receive window from rx-slot occupancy: linear scale between
 * full window when empty and hard zero when one slot from full, with a
 * small floor so a busy socket can still ACK-progress. */
u32 tcp_adv_from_slots(u32 queued_slots, u32 cap_slots, u32 budget_bytes) {
    if (cap_slots == 0u) return 0u;
    if (queued_slots >= cap_slots) return 0u;
    u32 free_slots = cap_slots - queued_slots;
    if (free_slots >= cap_slots) return budget_bytes;
    u32 wnd = (budget_bytes / cap_slots) * free_slots;
    if (wnd < TCP_ADV_FLOOR) wnd = TCP_ADV_FLOOR;
    if (wnd > budget_bytes) wnd = budget_bytes;
    return wnd;
}

u32 tcp_backoff_rto(u32 rto_ticks) {
    if (rto_ticks < TCP_RTO_MIN_TICKS) return TCP_RTO_MIN_TICKS;
    if (rto_ticks > TCP_RTO_MAX_TICKS) return TCP_RTO_MAX_TICKS;
    return rto_ticks;
}

void tcp_on_ack(u32* cwnd, u32 ssthresh, u32 mss) {
    if (!cwnd) return;
    if (mss == 0u) mss = TCP_DEFAULT_MSS;

    /* Slow Start: Exponential growth until ssthresh */
    if (*cwnd < ssthresh) {
        *cwnd += mss;
    } else {
        /* Congestion Avoidance: Linear AIMD growth (1 MSS per RTT) */
        u32 inc = (mss * mss) / (*cwnd > 0 ? *cwnd : 1);
        if (inc == 0u) inc = 1u;
        *cwnd += inc;
    }

    if (*cwnd > TCP_MAX_CWND) *cwnd = TCP_MAX_CWND;
}

void tcp_on_loss(u32* cwnd, u32* ssthresh, u32 mss) {
    if (!cwnd || !ssthresh) return;
    if (mss == 0u) mss = TCP_DEFAULT_MSS;

    /* Fast Retransmit / Multiplicative Decrease (halve window) */
    *ssthresh = *cwnd / 2u;
    if (*ssthresh < 2u * mss) *ssthresh = 2u * mss;
    *cwnd = *ssthresh;
}
