#include "ipv6_transport.h"
#include "ipv6.h"
#include "ethernet.h"
#include "udp_raw.h"
#include "tcp_raw.h"
#include "ip.h"
#include "nic.h"
#include "string.h"

static u8 g_ipv6_udp_frame[1514] __attribute__((aligned(16)));
static u8 g_ipv6_tcp_frame[1514] __attribute__((aligned(16)));

int ipv6_udp_send(const u8 src[16], const u8 dst[16], const u8 dst_mac[6], u16 src_port, u16 dst_port, const u8* data, u16 len) {
    u8* frame = g_ipv6_udp_frame; eth_header_t* eth = (eth_header_t*)frame;
    ipv6_header_t* ip = (ipv6_header_t*)(frame + sizeof(eth_header_t));
    udp_header_t* udp = (udp_header_t*)(frame + sizeof(eth_header_t) + sizeof(ipv6_header_t));
    u8* payload = (u8*)udp + sizeof(udp_header_t); u8 src_mac[6];
    u16 total = (u16)(sizeof(udp_header_t) + len);
    if (!src || !dst || !dst_mac || (!data && len) || total > 1200u || nic_get_mac(src_mac) != 0) return -1;
    if (ipv6_build_header(ip, src, dst, IPV6_NEXT_UDP, total, 64u) != 0) return -1;
    udp->src_port = ip_htons(src_port); udp->dst_port = ip_htons(dst_port); udp->len = ip_htons(total); udp->checksum = 0u;
    if (len) memcpy(payload, data, len);
    udp->checksum = ipv6_transport_checksum(ip->src, ip->dst, IPV6_NEXT_UDP, (const u8*)udp, total);
    for (u32 i = 0u; i < 6u; i++) { eth->src[i] = src_mac[i]; eth->dst[i] = dst_mac[i]; }
    eth->ethertype = ip_htons(ETH_TYPE_IPV6);
    return ethernet_send_frame(frame, (u16)(sizeof(eth_header_t) + sizeof(ipv6_header_t) + total));
}

int ipv6_tcp_send(const u8 src[16], const u8 dst[16], const u8 dst_mac[6], u16 src_port, u16 dst_port, u32 seq, u32 ack, u8 flags, const u8* data, u16 len) {
    u8* frame = g_ipv6_tcp_frame; eth_header_t* eth = (eth_header_t*)frame;
    ipv6_header_t* ip = (ipv6_header_t*)(frame + sizeof(eth_header_t));
    tcp_header_t* tcp = (tcp_header_t*)(frame + sizeof(eth_header_t) + sizeof(ipv6_header_t));
    u8* payload = (u8*)tcp + sizeof(tcp_header_t); u8 src_mac[6];
    u16 total = (u16)(sizeof(tcp_header_t) + len);
    if (!src || !dst || !dst_mac || (!data && len) || total > 1200u || nic_get_mac(src_mac) != 0) return -1;
    if (ipv6_build_header(ip, src, dst, IPV6_NEXT_TCP, total, 64u) != 0) return -1;
    tcp->src_port = ip_htons(src_port); tcp->dst_port = ip_htons(dst_port); tcp->seq = ip_htonl(seq); tcp->ack = ip_htonl(ack);
    tcp->data_offset = (u8)(5u << 4); tcp->flags = flags; tcp->window = ip_htons(8192u); tcp->checksum = 0u; tcp->urg_ptr = 0u;
    if (len) memcpy(payload, data, len);
    tcp->checksum = ipv6_transport_checksum(ip->src, ip->dst, IPV6_NEXT_TCP, (const u8*)tcp, total);
    for (u32 i = 0u; i < 6u; i++) { eth->src[i] = src_mac[i]; eth->dst[i] = dst_mac[i]; }
    eth->ethertype = ip_htons(ETH_TYPE_IPV6);
    return ethernet_send_frame(frame, (u16)(sizeof(eth_header_t) + sizeof(ipv6_header_t) + total));
}
