#include "ethernet.h"
#include "nic.h"
#include "wireguard.h"

static u8 g_local_mac[ETH_ADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static u8 g_peer_mac[ETH_ADDR_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

void ethernet_default_mac(u8* out_src, u8* out_dst) {
    if (out_src) {
        for (u32 i = 0; i < ETH_ADDR_LEN; i++) out_src[i] = g_local_mac[i];
    }
    if (out_dst) {
        for (u32 i = 0; i < ETH_ADDR_LEN; i++) out_dst[i] = g_peer_mac[i];
    }
}

void ethernet_set_local_mac(const u8* mac) {
    if (!mac) return;
    for (u32 i = 0; i < ETH_ADDR_LEN; i++) g_local_mac[i] = mac[i];
}

void ethernet_set_peer_mac(const u8* mac) {
    if (!mac) return;
    for (u32 i = 0; i < ETH_ADDR_LEN; i++) g_peer_mac[i] = mac[i];
}

int ethernet_send_frame(const u8* frame, u16 len) {
    if (!frame || len < sizeof(eth_header_t) || len > 1514) {
        return -1;
    }
    /* WireGuard policy: datagrams bound for the allowed network must go
     * through the tunnel; interception consumes them. */
    if (wireguard_intercept_outbound(frame, len)) {
        return 0;
    }
    return nic_send_frame(frame, len);
}
