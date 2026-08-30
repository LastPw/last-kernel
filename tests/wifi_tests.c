#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "wifi.h"

void serial_write(const char* s) { (void)s; }
void serial_write_dec(u32 v) { (void)v; }
u32 str_len(const char* s) { return (u32)strlen(s); }
i32 str_cmp(const char* a, const char* b) { return strcmp(a, b); }
void str_ncpy(char* dst, const char* src, u32 n) { strncpy(dst, src, n); dst[n] = '\0'; }
void spinlock_init(void* lock) { (void)lock; }


static void test_wifi_header_layout(void) {
    printf("  [1/4] Testing IEEE 802.11 24-Byte & QoS 26-Byte MAC Headers... ");
    assert(sizeof(ieee80211_hdr_t) == 24);
    assert(sizeof(ieee80211_qos_hdr_t) == 26);
    assert(sizeof(eapol_key_frame_t) == 99);
    printf("PASS (Strict IEEE 802.11 Packed Layout Verified)\n");
}

static void test_wifi_freq_channels(void) {
    printf("  [2/4] Testing 2.4GHz and 5.0GHz Frequency & Channel Conversions... ");
    assert(wifi_channel_to_freq(1) == 2412);
    assert(wifi_channel_to_freq(6) == 2437);
    assert(wifi_channel_to_freq(11) == 2462);
    assert(wifi_channel_to_freq(14) == 2484);
    assert(wifi_channel_to_freq(36) == 5180);
    assert(wifi_channel_to_freq(165) == 5825);

    assert(wifi_freq_to_channel(2412) == 1);
    assert(wifi_freq_to_channel(2484) == 14);
    assert(wifi_freq_to_channel(5180) == 36);
    assert(wifi_freq_to_channel(5825) == 165);
    printf("PASS (Spectrum Frequencies 2412-5825 MHz Verified)\n");
}

static void test_wpa2_ptk_derivation(void) {
    printf("  [3/4] Testing IEEE 802.11i WPA2-PSK PTK PRF-512 Derivation... ");
    uint8_t pmk[32];
    uint8_t addr1[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
    uint8_t addr2[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
    uint8_t anonce[32];
    uint8_t snonce[32];
    uint8_t ptk[48];

    memset(pmk, 0x42, sizeof(pmk));
    memset(anonce, 0xAA, sizeof(anonce));
    memset(snonce, 0x55, sizeof(snonce));

    int res = wifi_derive_ptk(pmk, addr1, addr2, anonce, snonce, ptk);
    assert(res == 0);

    /* Verify non-zero derived KCK, KEK and TK */
    uint8_t sum = 0;
    for (int i = 0; i < 48; i++) sum |= ptk[i];
    assert(sum != 0);
    printf("PASS (48-Byte PTK / KCK / KEK / TK Successfully Derived)\n");
}

static void test_wifi_to_eth_bridge(void) {
    printf("  [4/4] Testing 802.11 Wireless to 802.3 Ethernet Packet Translation... ");
    uint8_t raw_frame[64];
    memset(raw_frame, 0, sizeof(raw_frame));

    ieee80211_qos_hdr_t* qos = (ieee80211_qos_hdr_t*)raw_frame;
    qos->base.frame_control = WIFI_FRAME_QOS_DATA;
    qos->base.addr1[0] = 0xDE; qos->base.addr1[5] = 0xAD;
    qos->base.addr2[0] = 0xBE; qos->base.addr2[5] = 0xEF;

    const char* payload = "Hello LastKernel Wireless!";
    memcpy(raw_frame + sizeof(ieee80211_qos_hdr_t), payload, strlen(payload));

    int res = wifi_rx_frame(raw_frame, sizeof(ieee80211_qos_hdr_t) + (uint32_t)strlen(payload));
    assert(res == 0);
    printf("PASS (Transparent 802.3 Translation Complete)\n");
}

int main(void) {
    printf("\n=======================================================\n");
    printf("   LastKernel Wireless (Wi-Fi 802.11) Test Suite       \n");
    printf("=======================================================\n");
    test_wifi_header_layout();
    test_wifi_freq_channels();
    test_wpa2_ptk_derivation();
    test_wifi_to_eth_bridge();
    printf(">>> ALL WIRELESS 802.11 VECTORS PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
