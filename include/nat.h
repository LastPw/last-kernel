#ifndef NAT_H
#define NAT_H

#include "types.h"

/* ─── Connection tracking ───────────────────────────────────────────────── */

#define CONNTRACK_MAX 64u

/* Protocol-agnostic 5-tuple used as a connection key. */
typedef struct {
    u32  src_ip;
    u32  dst_ip;
    u16  src_port;                  /* 0 = ICMP identifier */
    u16  dst_port;
    u8   proto;                     /* IP_PROTO_TCP / IP_PROTO_UDP / IP_PROTO_ICMP */
} conntrack_key_t;

typedef struct {
    u8   valid;
    conntrack_key_t key;            /* (original-src, original-dst) 5-tuple */
    u32  timeout_tick;              /* expiry deadline */
    u8   state;                     /* CT_NEW / CT_ESTABLISHED / CT_CLOSING */

    /* NAT reverse mapping — enough to undo the translation on the return trip. */
    u32  nat_src_ip;                /* translated source IP   (SNAT out: our external IP) */
    u16  nat_src_port;              /* translated source port (SNAT out: ephemeral)       */
    u32  nat_dst_ip;                /* translated destination IP  (DNAT: internal IP)    */
    u16  nat_dst_port;              /* translated destination port                        */
} conntrack_entry_t;

/* TCP conntrack states */
#define CT_NEW         0u
#define CT_ESTABLISHED 1u
#define CT_CLOSING     2u

/* ─── NAT rules (port forwarding / SNAT policy) ──────────────────────────── */

#define NAT_RULE_MAX 16u

#define NAT_TYPE_SNAT 1u
#define NAT_TYPE_DNAT 2u

typedef struct {
    u8   valid;
    u8   type;                     /* NAT_TYPE_SNAT or NAT_TYPE_DNAT */
    u8   proto;                    /* IP_PROTO_TCP / IP_PROTO_UDP / 0 = any */
    u16  external_port;            /* For DNAT: port on our external IP */
    u32  internal_ip;             /* For DNAT: target internal IP */
    u16  internal_port;           /* For DNAT: target internal port (0 = same as external) */
    u32  internal_net;            /* For SNAT: source network */
    u32  internal_mask;           /* For SNAT: source netmask */
} nat_rule_t;

/* ─── IP forwarding state ────────────────────────────────────────────────── */

typedef struct {
    u8   ip_forward;               /* non-zero = kernel forwards packets */
    u32  ephemeral_next;           /* next ephemeral port for SNAT */
    u32  conntrack_count;
    u32  rule_count;
    u32  snat_packets;             /* statistics */
    u32  dnat_packets;
} nat_status_t;

/* ─── Public API ─────────────────────────────────────────────────────────── */

void nat_init(void);

/* Rule management */
int  nat_rule_add(u8 type, u8 proto, u16 external_port,
                  u32 internal_ip, u16 internal_port,
                  u32 internal_net, u32 internal_mask);
int  nat_rule_del(u32 index);
int  nat_rule_get(u32 index, nat_rule_t* out);
u32  nat_rule_count(void);

/* IP forwarding */
void nat_set_ip_forward(u8 on);
u8   nat_ip_forward(void);

/* Connection tracking inspection */
u32  nat_conntrack_count(void);
int  nat_conntrack_get(u32 index, conntrack_entry_t* out);

/* Main NAT processing hooks — called from netstack_on_frame().
   Returns 0 if the packet was consumed (NATted + forwarded),
   returns 1 if the packet should continue to normal socket processing,
   returns -1 if the packet should be dropped. */
int  nat_process_inbound(u32 src_ip, u32 dst_ip, u8 proto,
                          u16 src_port, u16 dst_port,
                          u8* l3_buf, u16 l3_len,
                          u8* frame, u16 frame_len);

/* Status */
void nat_status(nat_status_t* out);

#endif /* NAT_H */
