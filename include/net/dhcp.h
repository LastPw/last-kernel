#ifndef DHCP_H
#define DHCP_H

#include "types.h"

/* ─── Shared DHCP wire-protocol constants ───────────────────────────────────
 * The client (network/dhcp.c) and server (network/dhcp_server.c) speak the
 * same DHCP packet format, so the port numbers, magic cookie, message types
 * and option codes live here and are included by both.  These are RFC 2131/2132
 * fixed values, independent of either side's implementation. */

#define DHCP_CLIENT_PORT 68u
#define DHCP_SERVER_PORT 67u
#define DHCP_MAGIC       0x63825363u

/* DHCP message types (option 53 payloads) */
#define DHCP_DISCOVER    1u
#define DHCP_OFFER       2u
#define DHCP_REQUEST     3u
#define DHCP_DECLINE     4u
#define DHCP_ACK         5u
#define DHCP_NAK         6u
#define DHCP_RELEASE     7u

/* DHCP options */
#define DHCP_OPT_SUBNET       1u
#define DHCP_OPT_ROUTER       3u
#define DHCP_OPT_DNS          6u
#define DHCP_OPT_REQUESTED_IP 50u
#define DHCP_OPT_LEASE_TIME   51u
#define DHCP_OPT_MSG_TYPE     53u
#define DHCP_OPT_SERVER_ID    54u
#define DHCP_OPT_PARAM_LIST   55u
#define DHCP_OPT_END          255u

int dhcp_configure(u32 timeout_ticks);
int dhcp_ready(void);
u32 dhcp_server_ip(void);
u32 dhcp_dns_ip(void);

#endif
