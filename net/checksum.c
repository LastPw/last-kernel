#include "checksum.h"
#include "ip.h"
#include "string.h"

u16 net_checksum16(const u8* data, u32 len) {
    u32 sum = 0;
    u32 i = 0;

    while (i + 1 < len) {
        u16 word = (u16)(((u16)data[i] << 8) | data[i + 1]);
        sum += word;
        i += 2;
    }

    if (i < len) {
        sum += (u16)((u16)data[i] << 8);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (u16)(~sum);
}

static u16 pseudo_checksum(u32 src_ip, u32 dst_ip, u8 proto, const u8* payload, u16 payload_len) {
    u8 pseudo[12];
    u8 combined[12u + 1024u];
    pseudo[0] = (u8)((src_ip >> 24) & 0xFF);
    pseudo[1] = (u8)((src_ip >> 16) & 0xFF);
    pseudo[2] = (u8)((src_ip >> 8) & 0xFF);
    pseudo[3] = (u8)(src_ip & 0xFF);
    pseudo[4] = (u8)((dst_ip >> 24) & 0xFF);
    pseudo[5] = (u8)((dst_ip >> 16) & 0xFF);
    pseudo[6] = (u8)((dst_ip >> 8) & 0xFF);
    pseudo[7] = (u8)(dst_ip & 0xFF);
    pseudo[8] = 0;
    pseudo[9] = proto;
    pseudo[10] = (u8)((payload_len >> 8) & 0xFF);
    pseudo[11] = (u8)(payload_len & 0xFF);

    if (payload_len > 1024u || (payload_len > 0u && !payload)) return 0u;
    for (u32 i = 0u; i < sizeof(pseudo); i++) combined[i] = pseudo[i];
    if (payload_len) memcpy(combined + sizeof(pseudo), payload, payload_len);
    return net_checksum16(combined, sizeof(pseudo) + payload_len);
}

u16 net_tcp_checksum(u32 src_ip, u32 dst_ip, const u8* segment, u16 seg_len) {
    return pseudo_checksum(src_ip, dst_ip, IP_PROTO_TCP, segment, seg_len);
}

u16 net_udp_checksum(u32 src_ip, u32 dst_ip, const u8* datagram, u16 datagram_len) {
    return pseudo_checksum(src_ip, dst_ip, IP_PROTO_UDP, datagram, datagram_len);
}
