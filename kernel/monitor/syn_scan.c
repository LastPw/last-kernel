#include "syn_scan.h"
#include "tcp_raw.h"

int syn_scan_send(scan_entry_t* e, u32 source_ip, u16 source_port, u32 seq_seed) {
    if (!e) {
        return -1;
    }
    return tcp_raw_send_syn(source_ip, e->target_ip, source_port, e->port, seq_seed);
}

void syn_scan_on_tcp(scan_entry_t* e, u8 tcp_flags) {
    if (!e) {
        return;
    }
    if ((tcp_flags & TCP_FLAG_SYN) && (tcp_flags & TCP_FLAG_ACK)) {
        e->state = SCAN_STATE_OPEN;
        return;
    }
    if (tcp_flags & TCP_FLAG_RST) {
        e->state = SCAN_STATE_CLOSED;
    }
}
