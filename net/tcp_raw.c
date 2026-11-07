#include "tcp_raw.h"
#include "ethernet.h"
#include "ip.h"
#include "checksum.h"
#include "string.h"

int tcp_raw_send_data(u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port,
                      u32 seq, u32 ack, const u8* payload, u16 payload_len, u16 window) {
    u8 frame[14 + 20 + 20 + 1500];
    eth_header_t* eth = (eth_header_t*)frame;
    ipv4_header_t* ip = (ipv4_header_t*)(frame + sizeof(eth_header_t));
    tcp_header_t* tcp = (tcp_header_t*)((u8*)ip + sizeof(ipv4_header_t));
    u8* body = (u8*)tcp + sizeof(tcp_header_t);

    if (payload_len > 1460u || (payload_len > 0u && !payload)) return -1;
    ethernet_default_mac(eth->src, eth->dst);
    eth->ethertype = ip_htons(ETH_TYPE_IPV4);
    if (ip_build_ipv4(ip, src_ip, dst_ip, IP_PROTO_TCP,
                      (u16)(sizeof(tcp_header_t) + payload_len), (u16)(seq & 0xFFFFu)) != 0) {
        return -1;
    }
    tcp->src_port = ip_htons(src_port);
    tcp->dst_port = ip_htons(dst_port);
    tcp->seq = ip_htonl(seq);
    tcp->ack = ip_htonl(ack);
    tcp->data_offset = (u8)(5u << 4);
    tcp->flags = (u8)(TCP_FLAG_ACK | TCP_FLAG_PSH);
    tcp->window = ip_htons(window);
    tcp->checksum = 0u;
    tcp->urg_ptr = 0u;
    if (payload_len > 0u) memcpy(body, payload, payload_len);
    tcp->checksum = ip_htons(net_tcp_checksum(src_ip, dst_ip, (const u8*)tcp,
                                              (u16)(sizeof(tcp_header_t) + payload_len)));
    return ethernet_send_frame(frame, (u16)(sizeof(eth_header_t) + sizeof(ipv4_header_t) +
                                             sizeof(tcp_header_t) + payload_len));
}

int tcp_raw_send_syn(u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port, u32 seq) {
    u8 frame[14 + 20 + 20];
    eth_header_t* eth = (eth_header_t*)frame;
    ipv4_header_t* ip = (ipv4_header_t*)(frame + sizeof(eth_header_t));
    tcp_header_t* tcp = (tcp_header_t*)((u8*)ip + sizeof(ipv4_header_t));

    ethernet_default_mac(eth->src, eth->dst);
    eth->ethertype = ip_htons(ETH_TYPE_IPV4);

    if (ip_build_ipv4(ip, src_ip, dst_ip, IP_PROTO_TCP, sizeof(tcp_header_t), (u16)(seq & 0xFFFFu)) != 0) {
        return -1;
    }

    tcp->src_port = ip_htons(src_port);
    tcp->dst_port = ip_htons(dst_port);
    tcp->seq = ip_htonl(seq);
    tcp->ack = 0;
    tcp->data_offset = (u8)(5u << 4);
    tcp->flags = TCP_FLAG_SYN;
    tcp->window = ip_htons(65535u);
    tcp->checksum = 0;
    tcp->urg_ptr = 0;
    tcp->checksum = ip_htons(net_tcp_checksum(src_ip, dst_ip, (const u8*)tcp, sizeof(tcp_header_t)));

    return ethernet_send_frame(frame, sizeof(frame));
}

int tcp_raw_send_syn_ack(u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port, u32 seq, u32 ack, u16 window) {
    u8 frame[14 + sizeof(ipv4_header_t) + sizeof(tcp_header_t)];
    eth_header_t* eth = (eth_header_t*)frame;
    ipv4_header_t* ip = (ipv4_header_t*)(frame + sizeof(eth_header_t));
    tcp_header_t* tcp = (tcp_header_t*)((u8*)ip + sizeof(ipv4_header_t));
    ethernet_default_mac(eth->src, eth->dst);
    eth->ethertype = ip_htons(ETH_TYPE_IPV4);
    if (ip_build_ipv4(ip, src_ip, dst_ip, IP_PROTO_TCP, sizeof(tcp_header_t), (u16)(seq & 0xFFFFu)) != 0) return -1;
    tcp->src_port = ip_htons(src_port); tcp->dst_port = ip_htons(dst_port);
    tcp->seq = ip_htonl(seq); tcp->ack = ip_htonl(ack);
    tcp->data_offset = (u8)(5u << 4); tcp->flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
    tcp->window = ip_htons(window); tcp->checksum = 0u; tcp->urg_ptr = 0u;
    tcp->checksum = ip_htons(net_tcp_checksum(src_ip, dst_ip, (const u8*)tcp, sizeof(tcp_header_t)));
    return ethernet_send_frame(frame, sizeof(frame));
}

int tcp_raw_send_ack(u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port, u32 seq, u32 ack, u16 window) {
    u8 frame[14 + 20 + 20];
    eth_header_t* eth = (eth_header_t*)frame;
    ipv4_header_t* ip = (ipv4_header_t*)(frame + sizeof(eth_header_t));
    tcp_header_t* tcp = (tcp_header_t*)((u8*)ip + sizeof(ipv4_header_t));

    ethernet_default_mac(eth->src, eth->dst);
    eth->ethertype = ip_htons(ETH_TYPE_IPV4);

    if (ip_build_ipv4(ip, src_ip, dst_ip, IP_PROTO_TCP, sizeof(tcp_header_t), (u16)(seq & 0xFFFFu)) != 0) {
        return -1;
    }

    tcp->src_port = ip_htons(src_port);
    tcp->dst_port = ip_htons(dst_port);
    tcp->seq = ip_htonl(seq);
    tcp->ack = ip_htonl(ack);
    tcp->data_offset = (u8)(5u << 4);
    tcp->flags = TCP_FLAG_ACK;
    tcp->window = ip_htons(window);
    tcp->checksum = 0;
    tcp->urg_ptr = 0;
    tcp->checksum = ip_htons(net_tcp_checksum(src_ip, dst_ip, (const u8*)tcp, sizeof(tcp_header_t)));

    return ethernet_send_frame(frame, sizeof(frame));
}
