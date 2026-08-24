#ifndef LASTOS_AIRMESH_H
#define LASTOS_AIRMESH_H

#include "types.h"

#define ETH_TYPE_AIRMESH   0xA180
#define AIRMESH_MAGIC      0x4149524D /* "AIRM" */
#define AIRMESH_MAX_PEERS  16u
#define AIRMESH_MAX_PAYLOAD 512u

/* AirMesh PHY Medium Modes */
#define AIRMESH_PHY_ETHERNET       1u
#define AIRMESH_PHY_WIFI_ADHOC     2u
#define AIRMESH_PHY_WIFI_80211S    3u

/* AirMesh Protocol Message Types */
#define AIRMESH_MSG_BEACON         0x01
#define AIRMESH_MSG_KEY_EXCHANGE   0x02
#define AIRMESH_MSG_ENCRYPTED_DATA 0x03
#define AIRMESH_MSG_TEXT           0x04
#define AIRMESH_MSG_ROUTE_ANNOUNCE 0x05

/* AirMesh Peer Session States */
#define AIRMESH_SESSION_IDLE         0u
#define AIRMESH_SESSION_HANDSHAKE    1u
#define AIRMESH_SESSION_ESTABLISHED  2u

typedef struct __attribute__((packed)) {
    u32 magic;          /* AIRMESH_MAGIC */
    u8  version;        /* Protocol Version (1) */
    u8  msg_type;       /* AIRMESH_MSG_* */
    u8  hop_count;      /* Mesh hop TTL */
    u8  phy_mode;       /* AIRMESH_PHY_* */
    u32 src_node_id;    /* Unique 32-bit Node ID */
    u32 dst_node_id;    /* Target Node ID or 0xFFFFFFFF for Broadcast */
    char hostname[24];  /* Human-readable peer name */
    u32 session_id;     /* Session identifier */
    u32 seq_number;     /* Anti-replay sequence counter */
    u16 payload_len;    /* Payload length */
    u16 checksum;       /* Fletcher16 or CRC16 */
} airmesh_hdr_t;

typedef struct {
    u8  valid;
    u32 node_id;
    u8  mac[6];
    char hostname[24];
    u32 link_quality;   /* Link Quality / RSSI (0..100) */
    u32 hop_count;
    u32 session_state;  /* AIRMESH_SESSION_* */
    u32 packets_rx;
    u32 packets_tx;
    u32 last_seen_tick;
} airmesh_peer_t;

typedef struct {
    u32 local_node_id;
    char local_hostname[24];
    u32 phy_mode;
    u32 peer_count;
    u32 beacons_sent;
    u32 beacons_received;
    u32 messages_sent;
    u32 messages_received;
    airmesh_peer_t peers[AIRMESH_MAX_PEERS];
} airmesh_status_t;

/* AirMesh Engine Functions */
void airmesh_init(void);
void airmesh_tick(void);
int  airmesh_send_beacon(void);
int  airmesh_send_text(u32 target_node, const char* text);
void airmesh_on_rx_frame(const u8* frame, u16 len);
void airmesh_get_status(airmesh_status_t* out);
void airmesh_set_phy_mode(u32 mode);

#endif /* LASTOS_AIRMESH_H */
