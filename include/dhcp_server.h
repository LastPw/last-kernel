#ifndef DHCP_SERVER_H
#define DHCP_SERVER_H

#include "types.h"

/* ─── DHCP Server ───────────────────────────────────────────────────────────
 * Listens on UDP port 67, hands out IP addresses from a configurable pool,
 * tracks leases by MAC address with renewal and expiry.
 * ──────────────────────────────────────────────────────────────────────────── */

#define DHCP_SRV_LEASE_MAX 32u
#define DHCP_SRV_PACKET_MAX 548u

typedef struct {
    u8   valid;
    u8   mac[6];                    /* client MAC */
    u32  offered_ip;                /* leased IP */
    u32  lease_end_tick;            /* expiry deadline */
} dhcp_lease_t;

typedef struct {
    u8   active;                    /* non-zero while the server is listening */
    u32  pool_start;                /* first IP in pool */
    u32  pool_end;                  /* last IP in pool  */
    u32  subnet_mask;
    u32  gateway;
    u32  dns_server;
    u32  lease_time_ticks;          /* lease duration (default 6000 = ~60 s) */

    u32  offers_sent;
    u32  acks_sent;
    u32  declines_received;
    u32  leases_active;
} dhcp_server_status_t;

/* ─── API ─────────────────────────────────────────────────────────────────── */

/* Configure the pool, subnet, gateway, DNS.
   Returns 0 on success. */
int  dhcp_server_configure(u32 pool_start, u32 pool_end,
                            u32 subnet_mask, u32 gateway, u32 dns_server,
                            u32 lease_time_ticks);

/* Start / stop listening on UDP :67 */
int  dhcp_server_start(void);
void dhcp_server_stop(void);
int  dhcp_server_active(void);

/* Poll — call from netstack_poll() or timer tick */
void dhcp_server_poll(void);

/* Lease table inspection */
int  dhcp_server_lease_get(u32 index, dhcp_lease_t* out);
u32  dhcp_server_lease_count(void);

/* Status */
void dhcp_server_status(dhcp_server_status_t* out);

#endif /* DHCP_SERVER_H */
