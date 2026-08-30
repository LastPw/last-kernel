#ifndef UDP_RAW_H
#define UDP_RAW_H

#include "types.h"

typedef struct __attribute__((packed)) {
    u16 src_port;
    u16 dst_port;
    u16 len;
    u16 checksum;
} udp_header_t;

int udp_raw_send_probe(u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port);
int udp_raw_send(u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port, const u8* payload, u16 payload_len);

#endif
