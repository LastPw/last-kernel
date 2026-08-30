#ifndef CHECKSUM_H
#define CHECKSUM_H

#include "types.h"

u16 net_checksum16(const u8* data, u32 len);
u16 net_tcp_checksum(u32 src_ip, u32 dst_ip, const u8* segment, u16 seg_len);
u16 net_udp_checksum(u32 src_ip, u32 dst_ip, const u8* datagram, u16 datagram_len);

#endif
