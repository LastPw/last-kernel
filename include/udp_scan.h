#ifndef UDP_SCAN_H
#define UDP_SCAN_H

#include "types.h"
#include "scan_table.h"

int udp_scan_send(scan_entry_t* e, u32 source_ip, u16 source_port);
void udp_scan_on_udp(scan_entry_t* e, const u8* payload, u16 payload_len);

#endif
