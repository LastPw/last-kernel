#include "udp_scan.h"
#include "udp_raw.h"

int udp_scan_send(scan_entry_t* e, u32 source_ip, u16 source_port) {
    if (!e) {
        return -1;
    }
    return udp_raw_send_probe(source_ip, e->target_ip, source_port, e->port);
}

void udp_scan_on_udp(scan_entry_t* e, const u8* payload, u16 payload_len) {
    (void)payload;
    (void)payload_len;
    if (!e) {
        return;
    }
    e->state = SCAN_STATE_OPEN;
}
