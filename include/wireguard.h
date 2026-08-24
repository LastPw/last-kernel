#ifndef WIREGUARD_H
#define WIREGUARD_H

/* In-kernel WireGuard v1 transport (Noise_IKpsk2 over UDP).
 * Single-peer configuration, symmetric roles: initiates when sending,
 * accepts incoming handshakes on the listen port.
 * Honest limitations vs full WireGuard:
 *  - mac2/cookie replies under load: not implemented (mac2 sent as zeros)
 *  - no roaming (endpoint fixed at configure time)
 *  - IP-layer tunnel routing is a follow-up; data API is message-oriented. */

#include "types.h"

#define WIREGUARD_UDP_PORT   51820u
#define WIREGUARD_KEY_HEX    64u
#define WIREGUARD_PSK_HEX    64u

typedef struct {
    u8  configured;
    u8  active;             /* transport session established */
    u8  role_last_handshake;/* 1 = we initiated, 2 = they did */
    u32 endpoint_ip;
    u16 endpoint_port;
    u16 listen_port;
    u32 allowed_network;
    u32 allowed_mask;
    u8  peer_pub_hex[WIREGUARD_KEY_HEX + 1u];
    u8  session_local_index[4];
    u8  session_remote_index[4];
    u64 tx_counter;
    u64 rx_packets;
    u64 tx_packets;
    u32 handshake_count;
    u32 replay_drops;
    u32 auth_failures;
    u32 session_age_ticks;
    u32 last_handshake_tick;
} wireguard_status_t;

int  wireguard_init(void);

/* Keys are raw 32-byte values (already clamped for the private key).
 * psk may be NULL for all-zero PSK. Returns 0 on success. */
int  wireguard_configure(u32 endpoint_ip, u16 endpoint_port,
                         const u8 private_key[32], const u8 peer_public_key[32],
                         const u8 psk_or_null[32],
                         u32 allowed_net, u32 allowed_mask);
int  wireguard_start(void);
int  wireguard_stop(void);
void wireguard_clear_keys(void);
void wireguard_status(wireguard_status_t* out);
u16  wireguard_listen_port(void);
u8   wireguard_active(void);

/* netstack hooks. on_udp returns 1 if the datagram was consumed. */
int  wireguard_on_udp(u32 src_ip, u16 src_port, u16 dst_port,
                      const u8* data, u16 len);
void wireguard_tick(void);

/* Message-oriented encrypted send over the established session. */
int  wireguard_send_data(const void* payload, u16 len);

#endif /* WIREGUARD_H */
