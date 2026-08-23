#ifndef SYN_SCAN_H
#define SYN_SCAN_H

#include "types.h"
#include "scan_table.h"

int syn_scan_send(scan_entry_t* e, u32 source_ip, u16 source_port, u32 seq_seed);
void syn_scan_on_tcp(scan_entry_t* e, u8 tcp_flags);

#endif
