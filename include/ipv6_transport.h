#ifndef IPV6_TRANSPORT_H
#define IPV6_TRANSPORT_H

#include "types.h"

int ipv6_udp_send(const u8 src[16], const u8 dst[16], const u8 dst_mac[6], u16 src_port, u16 dst_port, const u8* data, u16 len);
int ipv6_tcp_send(const u8 src[16], const u8 dst[16], const u8 dst_mac[6], u16 src_port, u16 dst_port, u32 seq, u32 ack, u8 flags, const u8* data, u16 len);

#endif
