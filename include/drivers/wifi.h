#ifndef DRIVERS_NET_WIFI_H
#define DRIVERS_NET_WIFI_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_SSID_LEN        32u
#define WIFI_MAX_AP_COUNT        32u
#define WIFI_MAC_ADDR_LEN        6u

/* 802.11 Frame Types & Subtypes */
#define WIFI_FC_TYPE_MGMT        0x00u
#define WIFI_FC_TYPE_CTRL        0x01u
#define WIFI_FC_TYPE_DATA        0x02u

#define WIFI_FRAME_ASSOC_REQ     0x00u
#define WIFI_FRAME_ASSOC_RESP    0x10u
#define WIFI_FRAME_PROBE_REQ     0x40u
#define WIFI_FRAME_PROBE_RESP    0x50u
#define WIFI_FRAME_BEACON        0x80u
#define WIFI_FRAME_DISASSOC      0xA0u
#define WIFI_FRAME_AUTH          0xB0u
#define WIFI_FRAME_DEAUTH        0xC0u
#define WIFI_FRAME_DATA          0x08u
#define WIFI_FRAME_QOS_DATA      0x88u

/* Security Suites */
typedef enum {
    WIFI_SEC_OPEN    = 0,
    WIFI_SEC_WEP     = 1,
    WIFI_SEC_WPA_PSK = 2,
    WIFI_SEC_WPA2_PSK= 3,
    WIFI_SEC_WPA3_SAE= 4,
} wifi_security_t;

/* WPA2 Handshake Connection State */
typedef enum {
    WIFI_STATE_DISCONNECTED   = 0,
    WIFI_STATE_SCANNING       = 1,
    WIFI_STATE_AUTHENTICATING = 2,
    WIFI_STATE_ASSOCIATING    = 3,
    WIFI_STATE_HANDSHAKE_M1   = 4,
    WIFI_STATE_HANDSHAKE_M2   = 5,
    WIFI_STATE_HANDSHAKE_M3   = 6,
    WIFI_STATE_HANDSHAKE_M4   = 7,
    WIFI_STATE_CONNECTED      = 8,
} wifi_state_t;

/* IEEE 802.11 24-Byte MAC Header */
typedef struct {
    u16 frame_control;        /* Type, Subtype, ToDS, FromDS, Retry, Power, WEP */
    u16 duration_id;
    u8  addr1[WIFI_MAC_ADDR_LEN]; /* Destination Address / Receiver */
    u8  addr2[WIFI_MAC_ADDR_LEN]; /* Source Address / Transmitter */
    u8  addr3[WIFI_MAC_ADDR_LEN]; /* BSSID / Access Point Address */
    u16 sequence_ctrl;        /* Fragment:4, Sequence:12 */
} __attribute__((packed)) ieee80211_hdr_t;

/* IEEE 802.11 QoS Data 26-Byte Header */
typedef struct {
    ieee80211_hdr_t base;
    u16 qos_control;          /* TID:4, EOSP:1, AckPolicy:2, Reserved:9 */
} __attribute__((packed)) ieee80211_qos_hdr_t;

/* EAPOL-Key Frame Header (95 Bytes for 802.11i / WPA2) */
typedef struct {
    u8  version;              /* 1 or 2 */
    u8  type;                 /* 3 = EAPOL-Key */
    u16 length;
    u8  descriptor_type;      /* 2 = RSN / WPA2 */
    u16 key_info;             /* Key Type, Index, MIC, Encrypted */
    u16 key_length;           /* 16 Bytes for CCMP (AES-128) */
    u64 replay_counter;
    u8  key_nonce[32];        /* ANonce or SNonce */
    u8  key_iv[16];
    u8  key_rsc[8];
    u8  key_id[8];
    u8  key_mic[16];          /* HMAC-SHA1-128 MIC */
    u16 key_data_length;
} __attribute__((packed)) eapol_key_frame_t;

/* Access Point Structure */
typedef struct {
    char            ssid[WIFI_MAX_SSID_LEN + 1];
    u8              bssid[WIFI_MAC_ADDR_LEN];
    u8              channel;
    u16             frequency_mhz;
    s8              rssi_dbm;
    wifi_security_t security;
    u32             max_rate_mbps;
} wifi_ap_t;

/* In-Memory Wi-Fi Adapter State */
typedef struct {
    u8              is_initialized;
    u8              mac_address[WIFI_MAC_ADDR_LEN];
    wifi_state_t    state;
    wifi_ap_t       connected_ap;
    u8              pmk[32];  /* Pairwise Master Key (PBKDF2 SHA-1) */
    u8              anonce[32];
    u8              snonce[32];
    u8              kck[16];  /* Key Confirmation Key (for MIC) */
    u8              kek[16];  /* Key Encryption Key */
    u8              tk[16];   /* Temporal Key (for AES-CCMP) */
    u32             scan_count;
    wifi_ap_t       scan_results[WIFI_MAX_AP_COUNT];
} wifi_adapter_t;

/* Public API */
int  wifi_init(void);
int  wifi_scan(wifi_ap_t* out_results, u32 max_count);
int  wifi_connect(const char* ssid, const char* passphrase);
int  wifi_disconnect(void);
int  wifi_derive_ptk(const u8 pmk[32], const u8 addr1[WIFI_MAC_ADDR_LEN], const u8 addr2[WIFI_MAC_ADDR_LEN],
                     const u8 nonce1[32], const u8 nonce2[32], u8 out_ptk[48]);
u16  wifi_channel_to_freq(u8 channel);
u8   wifi_freq_to_channel(u16 freq_mhz);
int  wifi_rx_frame(const u8* frame_data, u32 len);
int  wifi_tx_packet(const u8* eth_packet, u32 len);
int  wifi_rx_frame_bridge(const u8* frame_data, u32 frame_len, u8* out_eth_data, u32 max_eth_len);
int  wifi_tx_packet_bridge(const u8* eth_data, u32 eth_len, u8* out_frame_data, u32 max_frame_len);
void wifi_get_status(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_NET_WIFI_H */
