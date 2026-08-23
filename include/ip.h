#ifndef IP_H
#define IP_H

#include "types.h"

#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17
#define IP_PROTO_ICMP 1
#define IP_PROTO_RAW 255

typedef struct __attribute__((packed)) {
    u8 ver_ihl;
    u8 tos;
    u16 total_len;
    u16 id;
    u16 flags_frag;
    u8 ttl;
    u8 proto;
    u16 checksum;
    u32 src_ip;
    u32 dst_ip;
} ipv4_header_t;

u16 ip_htons(u16 x);
u32 ip_htonl(u32 x);
u32 ip_parse_ipv4(const char* text, int* ok);
void ip_format_ipv4(u32 ip, char* out, u32 out_size);
int ip_build_ipv4(ipv4_header_t* ip, u32 src_ip, u32 dst_ip, u8 proto, u16 payload_len, u16 ident);

#endif
