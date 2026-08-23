#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include "types.h"

/* ─── DNS Server ────────────────────────────────────────────────────────────
 * Listens on UDP port 53, answers A / AAAA queries from the local DNS cache,
 * or forwards to the configured upstream DNS server.
 * ──────────────────────────────────────────────────────────────────────────── */

#define DNS_SRV_PACKET_MAX 512u

typedef struct {
    u8  active;                     /* non-zero while the server is listening */
    u32 queries_total;              /* statistics */
    u32 queries_cached;            /* answered from local cache */
    u32 queries_forwarded;         /* forwarded to upstream */
    u32 queries_failed;
    u32 queries_throttled;         /* dropped by the per-source rate limiter */
} dns_server_status_t;

/* Start / stop */
int  dns_server_start(void);                    /* bind UDP :53 */
void dns_server_stop(void);
int  dns_server_active(void);

/* Poll — call from netstack_poll() or timer tick */
void dns_server_poll(void);

/* Status */
void dns_server_status(dns_server_status_t* out);

#endif /* DNS_SERVER_H */
