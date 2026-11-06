#include "ip.h"
#include "checksum.h"
#include "string.h"

u16 ip_htons(u16 x) {
    return (u16)((x << 8) | (x >> 8));
}

u32 ip_htonl(u32 x) {
    return ((x & 0x000000FFu) << 24) |
           ((x & 0x0000FF00u) << 8) |
           ((x & 0x00FF0000u) >> 8) |
           ((x & 0xFF000000u) >> 24);
}

static int parse_octet(const char** p, u32* out) {
    u32 v = 0;
    int seen = 0;
    while (**p >= '0' && **p <= '9') {
        seen = 1;
        v = (v * 10u) + (u32)(**p - '0');
        if (v > 255u) {
            return -1;
        }
        (*p)++;
    }
    if (!seen) {
        return -1;
    }
    *out = v;
    return 0;
}

u32 ip_parse_ipv4(const char* text, int* ok) {
    u32 a = 0, b = 0, c = 0, d = 0;
    if (ok) {
        *ok = 0;
    }
    if (!text) {
        return 0;
    }
    if (parse_octet(&text, &a) != 0 || *text != '.') return 0;
    text++;
    if (parse_octet(&text, &b) != 0 || *text != '.') return 0;
    text++;
    if (parse_octet(&text, &c) != 0 || *text != '.') return 0;
    text++;
    if (parse_octet(&text, &d) != 0 || *text != '\0') return 0;
    if (ok) {
        *ok = 1;
    }
    return (a << 24) | (b << 16) | (c << 8) | d;
}

void ip_format_ipv4(u32 ip, char* out, u32 out_size) {
    if (!out || out_size < 8u) return;
    u32 a = (ip >> 24) & 0xFFu;
    u32 b = (ip >> 16) & 0xFFu;
    u32 c = (ip >> 8) & 0xFFu;
    u32 d = ip & 0xFFu;

    char tmp[4][4];
    u32 vals[4] = { a, b, c, d };
    for (u32 i = 0; i < 4u; i++) {
        u32 v = vals[i];
        if (v >= 100u) {
            tmp[i][0] = (char)('0' + (v / 100u));
            tmp[i][1] = (char)('0' + ((v / 10u) % 10u));
            tmp[i][2] = (char)('0' + (v % 10u));
            tmp[i][3] = '\0';
        } else if (v >= 10u) {
            tmp[i][0] = (char)('0' + (v / 10u));
            tmp[i][1] = (char)('0' + (v % 10u));
            tmp[i][2] = '\0';
        } else {
            tmp[i][0] = (char)('0' + v);
            tmp[i][1] = '\0';
        }
    }

    out[0] = '\0';
    str_ncpy(out, tmp[0], out_size);
    str_ncat(out, ".", out_size);
    str_ncat(out, tmp[1], out_size);
    str_ncat(out, ".", out_size);
    str_ncat(out, tmp[2], out_size);
    str_ncat(out, ".", out_size);
    str_ncat(out, tmp[3], out_size);
}

int ip_build_ipv4(ipv4_header_t* ip, u32 src_ip, u32 dst_ip, u8 proto, u16 payload_len, u16 ident) {
    if (!ip) {
        return -1;
    }

    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = ip_htons((u16)(sizeof(ipv4_header_t) + payload_len));
    ip->id = ip_htons(ident);
    ip->flags_frag = ip_htons(0x4000);
    ip->ttl = 64;
    ip->proto = proto;
    ip->checksum = 0;
    ip->src_ip = ip_htonl(src_ip);
    ip->dst_ip = ip_htonl(dst_ip);
    ip->checksum = ip_htons(net_checksum16((const u8*)ip, sizeof(ipv4_header_t)));
    return 0;
}
