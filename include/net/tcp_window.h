#ifndef TCP_WINDOW_H
#define TCP_WINDOW_H

#include "types.h"

/* Pure TCP flow-control helpers -- host-unit-testable, no kernel deps. */

u32 tcp_eff_window(u32 cwnd_bytes, u32 peer_wnd_bytes);
int  tcp_can_send(u32 unacked_bytes, u32 len, u32 eff_window);
u32  tcp_adv_from_slots(u32 queued_slots, u32 cap_slots, u32 budget_bytes);
u32  tcp_backoff_rto(u32 rto_ticks);
void tcp_on_ack(u32* cwnd, u32 ssthresh, u32 mss);
void tcp_on_loss(u32* cwnd, u32* ssthresh, u32 mss);

#define TCP_RTO_MIN_TICKS 40u
#define TCP_RTO_MAX_TICKS 4000u
#define TCP_ADV_FLOOR     512u
#define TCP_DEFAULT_MSS   1460u
#define TCP_MAX_CWND      (1024u * 1024u * 16u) /* 16 MB max BDP */

#endif
