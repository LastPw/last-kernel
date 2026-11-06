#include "udp_raw.h"
#include "ethernet.h"
#include "ip.h"
#include "checksum.h"
#include "string.h"

int udp_raw_send(u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port, const u8* payload, u16 payload_len) {
    u8 frame[14 + 20 + 8 + 1024];
    eth_header_t* eth = (eth_header_t*)frame;
    ipv4_header_t* ip = (ipv4_header_t*)(frame + sizeof(eth_header_t));
    udp_header_t* udp = (udp_header_t*)((u8*)ip + sizeof(ipv4_header_t));
    u8* body = (u8*)udp + sizeof(udp_header_t);

    if (payload_len > 1024u || (payload_len > 0u && !payload)) return -1;
    if (payload_len > 0u) memcpy(body, payload, payload_len);

    ethernet_default_mac(eth->src, eth->dst);
    eth->ethertype = ip_htons(ETH_TYPE_IPV4);

    if (ip_build_ipv4(ip, src_ip, dst_ip, IP_PROTO_UDP,
                      (u16)(sizeof(udp_header_t) + payload_len), (u16)(src_port ^ dst_port)) != 0) {
        return -1;
    }

    udp->src_port = ip_htons(src_port);
    udp->dst_port = ip_htons(dst_port);
    udp->len = ip_htons((u16)(sizeof(udp_header_t) + payload_len));
    udp->checksum = 0;
    udp->checksum = ip_htons(net_udp_checksum(src_ip, dst_ip, (const u8*)udp,
                                              (u16)(sizeof(udp_header_t) + payload_len)));

    return ethernet_send_frame(frame, (u16)(sizeof(eth_header_t) + sizeof(ipv4_header_t) +
                                             sizeof(udp_header_t) + payload_len));
}

int udp_raw_send_probe(u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port) {
    return udp_raw_send(src_ip, dst_ip, src_port, dst_port, 0, 0u);
}
