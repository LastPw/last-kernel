#ifndef TCP_RAW_H
#define TCP_RAW_H

#include "types.h"

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

typedef struct __attribute__((packed)) {
    u16 src_port;
    u16 dst_port;
    u32 seq;
    u32 ack;
    u8 data_offset;
    u8 flags;
    u16 window;
    u16 checksum;
    u16 urg_ptr;
} tcp_header_t;

int tcp_raw_send_syn(u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port, u32 seq);
int tcp_raw_send_syn_ack(u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port, u32 seq, u32 ack, u16 window);
int tcp_raw_send_ack(u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port, u32 seq, u32 ack, u16 window);
int tcp_raw_send_data(u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port,
                      u32 seq, u32 ack, const u8* payload, u16 payload_len, u16 window);

#endif
