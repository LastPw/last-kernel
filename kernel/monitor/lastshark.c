#include "lastshark.h"
#include "kernel_api.h"
#include "ethernet.h"
#include "ip.h"
#include "tcp_raw.h"
#include "udp_raw.h"
#include "string.h"

#define LSHARK_RING_MAX 128

typedef struct {
    u32 tick;
    u32 src_ip;
    u32 dst_ip;
    u16 src_port;
    u16 dst_port;
    u16 length;
    u8 proto;
    u8 flags;
    u8 used;
} lshark_entry_t;

typedef struct {
    u8 active;
    u8 filter;
    u32 head;
    u32 count;
    u32 total_seen;
    u32 total_captured;
    u32 dropped;
    u32 tcp_count;
    u32 udp_count;
    u32 icmp_count;
    u32 other_count;
    u32 pps_last;
    u32 pps_now;
    u32 last_sec_tick;
    lshark_entry_t ring[LSHARK_RING_MAX];
} lshark_state_t;

static lshark_state_t g;

static const char* skip_space(const char* s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static int parse_u32(const char* s, u32* out) {
    u32 v = 0;
    int seen = 0;
    s = skip_space(s);
    while (*s >= '0' && *s <= '9') {
        seen = 1;
        v = (v * 10u) + (u32)(*s - '0');
        s++;
    }
    if (!seen) {
        return -1;
    }
    *out = v;
    return 0;
}

static void print_u32_local(u32 value) {
    char buf[16];
    int i = 0;
    if (value == 0) {
        kputc('0');
        return;
    }
    while (value > 0 && i < (int)sizeof(buf) - 1) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        kputc(buf[--i]);
    }
}

static int filter_match(u8 proto) {
    if (g.filter == LSHARK_FILTER_ALL) {
        return 1;
    }
    if (g.filter == LSHARK_FILTER_TCP && proto == IP_PROTO_TCP) {
        return 1;
    }
    if (g.filter == LSHARK_FILTER_UDP && proto == IP_PROTO_UDP) {
        return 1;
    }
    if (g.filter == LSHARK_FILTER_ICMP && proto == 1) {
        return 1;
    }
    return 0;
}

static const char* proto_name(u8 proto) {
    if (proto == IP_PROTO_TCP) return "TCP";
    if (proto == IP_PROTO_UDP) return "UDP";
    if (proto == 1) return "ICMP";
    return "OTHER";
}

static void print_help(void) {
    kputs("lastshark start|stop|stats|clear\n");
    kputs("lastshark show [count]\n");
    kputs("lastshark filter all|tcp|udp|icmp\n");
}

void lastshark_init(void) {
    g.active = 0;
    g.filter = LSHARK_FILTER_ALL;
    g.head = 0;
    g.count = 0;
    g.total_seen = 0;
    g.total_captured = 0;
    g.dropped = 0;
    g.tcp_count = 0;
    g.udp_count = 0;
    g.icmp_count = 0;
    g.other_count = 0;
    g.pps_last = 0;
    g.pps_now = 0;
    g.last_sec_tick = kticks();
}

void lastshark_tick(void) {
    u32 now = kticks();
    if ((now - g.last_sec_tick) >= 100u) {
        g.pps_last = g.pps_now;
        g.pps_now = 0;
        g.last_sec_tick = now;
    }
}

void lastshark_on_frame(const u8* frame, u16 len) {
    const eth_header_t* eth;
    lshark_entry_t* e;
    const u8* l3;
    u16 l3_len;
    const ipv4_header_t* ip;
    u8 ihl;
    u8 proto;
    u16 src_port;
    u16 dst_port;
    u8 flags;
    const u8* l4;
    u16 l4_len;

    if (!frame || len < sizeof(eth_header_t)) {
        return;
    }

    g.total_seen++;

    eth = (const eth_header_t*)frame;
    if (ip_htons(eth->ethertype) != ETH_TYPE_IPV4) {
        g.other_count++;
        if (!g.active || !filter_match(0)) {
            return;
        }
        e = &g.ring[g.head];
        e->used = 1;
        e->tick = kticks();
        e->src_ip = 0;
        e->dst_ip = 0;
        e->src_port = 0;
        e->dst_port = 0;
        e->length = len;
        e->proto = 0;
        e->flags = 0;
        g.head = (g.head + 1u) % LSHARK_RING_MAX;
        if (g.count < LSHARK_RING_MAX) {
            g.count++;
        } else {
            g.dropped++;
        }
        g.total_captured++;
        g.pps_now++;
        return;
    }

    if (len < sizeof(eth_header_t) + sizeof(ipv4_header_t)) {
        return;
    }

    l3 = frame + sizeof(eth_header_t);
    l3_len = (u16)(len - sizeof(eth_header_t));
    ip = (const ipv4_header_t*)l3;
    ihl = (u8)((ip->ver_ihl & 0x0F) * 4u);
    if (ihl < sizeof(ipv4_header_t) || ihl > l3_len) {
        return;
    }

    proto = ip->proto;
    if (!filter_match(proto)) {
        return;
    }

    src_port = 0;
    dst_port = 0;
    flags = 0;
    l4 = l3 + ihl;
    l4_len = (u16)(l3_len - ihl);

    if (proto == IP_PROTO_TCP && l4_len >= sizeof(tcp_header_t)) {
        const tcp_header_t* tcp = (const tcp_header_t*)l4;
        src_port = ip_htons(tcp->src_port);
        dst_port = ip_htons(tcp->dst_port);
        flags = tcp->flags;
        g.tcp_count++;
    } else if (proto == IP_PROTO_UDP && l4_len >= sizeof(udp_header_t)) {
        const udp_header_t* udp = (const udp_header_t*)l4;
        src_port = ip_htons(udp->src_port);
        dst_port = ip_htons(udp->dst_port);
        g.udp_count++;
    } else if (proto == 1) {
        g.icmp_count++;
    } else {
        g.other_count++;
    }

    if (!g.active) {
        return;
    }

    e = &g.ring[g.head];
    e->used = 1;
    e->tick = kticks();
    e->src_ip = ip_htonl(ip->src_ip);
    e->dst_ip = ip_htonl(ip->dst_ip);
    e->src_port = src_port;
    e->dst_port = dst_port;
    e->length = len;
    e->proto = proto;
    e->flags = flags;

    g.head = (g.head + 1u) % LSHARK_RING_MAX;
    if (g.count < LSHARK_RING_MAX) {
        g.count++;
    } else {
        g.dropped++;
    }
    g.total_captured++;
    g.pps_now++;
}

static void print_entry(const lshark_entry_t* e) {
    char src[20];
    char dst[20];
    ip_format_ipv4(e->src_ip, src, sizeof(src));
    ip_format_ipv4(e->dst_ip, dst, sizeof(dst));

    kputs(src);
    kputs(":");
    print_u32_local(e->src_port);
    kputs(" -> ");
    kputs(dst);
    kputs(":");
    print_u32_local(e->dst_port);
    kputs(" ");
    kputs(proto_name(e->proto));
    if (e->proto == IP_PROTO_TCP) {
        kputs(" f=");
        print_u32_local(e->flags);
    }
    kputs(" len=");
    print_u32_local(e->length);
    kputc('\n');
}

static void print_stats(void) {
    kputs("lastshark status: ");
    kputs(g.active ? "running\n" : "stopped\n");
    kputs("filter: ");
    if (g.filter == LSHARK_FILTER_TCP) kputs("tcp\n");
    else if (g.filter == LSHARK_FILTER_UDP) kputs("udp\n");
    else if (g.filter == LSHARK_FILTER_ICMP) kputs("icmp\n");
    else kputs("all\n");

    kputs("seen=");
    print_u32_local(g.total_seen);
    kputs(" captured=");
    print_u32_local(g.total_captured);
    kputs(" ring=");
    print_u32_local(g.count);
    kputs(" dropped=");
    print_u32_local(g.dropped);
    kputc('\n');

    kputs("pps=");
    print_u32_local(g.pps_last);
    kputs(" tcp=");
    print_u32_local(g.tcp_count);
    kputs(" udp=");
    print_u32_local(g.udp_count);
    kputs(" icmp=");
    print_u32_local(g.icmp_count);
    kputs(" other=");
    print_u32_local(g.other_count);
    kputc('\n');
}

static void set_filter(const char* arg) {
    if (str_cmp(arg, "all") == 0) {
        g.filter = LSHARK_FILTER_ALL;
    } else if (str_cmp(arg, "tcp") == 0) {
        g.filter = LSHARK_FILTER_TCP;
    } else if (str_cmp(arg, "udp") == 0) {
        g.filter = LSHARK_FILTER_UDP;
    } else if (str_cmp(arg, "icmp") == 0) {
        g.filter = LSHARK_FILTER_ICMP;
    } else {
        kputs("Invalid filter. use all|tcp|udp|icmp\n");
        return;
    }
    kputs("lastshark filter updated.\n");
}

static void show_packets(u32 want) {
    u32 i;
    if (g.count == 0) {
        kputs("(no packets)\n");
        return;
    }
    if (want == 0 || want > g.count) {
        want = g.count;
    }

    for (i = 0; i < want; i++) {
        u32 idx = (g.head + LSHARK_RING_MAX - 1u - i) % LSHARK_RING_MAX;
        const lshark_entry_t* e = &g.ring[idx];
        if (!e->used) {
            continue;
        }
        print_entry(e);
    }
}

int lastshark_command(const char* args) {
    u32 i;
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "stats") == 0) {
        print_stats();
        return 0;
    }
    if (str_cmp(args, "start") == 0) {
        g.active = 1;
        kputs("lastshark capture started.\n");
        return 0;
    }
    if (str_cmp(args, "stop") == 0) {
        g.active = 0;
        kputs("lastshark capture stopped.\n");
        return 0;
    }
    if (str_cmp(args, "clear") == 0) {
        g.head = 0;
        g.count = 0;
        g.dropped = 0;
        for (i = 0; i < LSHARK_RING_MAX; i++) {
            g.ring[i].used = 0;
        }
        kputs("lastshark ring cleared.\n");
        return 0;
    }
    if (str_ncmp(args, "filter ", 7) == 0) {
        set_filter(skip_space(args + 7));
        return 0;
    }
    if (str_ncmp(args, "show", 4) == 0) {
        u32 n = 16;
        const char* p = skip_space(args + 4);
        if (*p) {
            if (parse_u32(p, &n) != 0) {
                kputs("Usage: lastshark show [count]\n");
                return -1;
            }
        }
        show_packets(n);
        return 0;
    }

    print_help();
    return -1;
}
