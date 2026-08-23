#ifndef IPV6_H
#define IPV6_H

#include "types.h"

#define IPV6_NEXT_TCP 6u
#define IPV6_NEXT_UDP 17u
#define IPV6_NEXT_ICMP 58u

typedef struct __attribute__((packed)) {
    u32 version_class_flow;
    u16 payload_len;
    u8 next_header;
    u8 hop_limit;
    u8 src[16];
    u8 dst[16];
} ipv6_header_t;

typedef struct __attribute__((packed)) {
    u8 type;
    u8 code;
    u16 checksum;
} icmpv6_header_t;

int ipv6_link_local_from_mac(const u8* mac, u8 out_addr[16]);
void ipv6_format(const u8 addr[16], char* out, u32 out_len);
int ipv6_parse(const char* text, u8 out_addr[16]);
int ipv6_build_header(ipv6_header_t* header, const u8 src[16], const u8 dst[16],
                      u8 next_header, u16 payload_len, u8 hop_limit);
u16 ipv6_transport_checksum(const u8 src[16], const u8 dst[16], u8 next_header,
                            const u8* payload, u16 payload_len);

#endif
