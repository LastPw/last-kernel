#include "wireguard.h"
#include "string.h"

static wireguard_status_t g_wg;

static int key_valid(const u8 key[32]) {
    u8 nonzero = 0u;
    if (!key) return 0;
    for (u32 i = 0u; i < 32u; i++) nonzero |= key[i];
    return nonzero != 0u;
}

int wireguard_configure(const char* endpoint, u16 port, const u8 private_key[32],
                        const u8 peer_key[32], u32 allowed_network, u32 allowed_mask) {
    u32 len;
    if (!endpoint || !endpoint[0] || port == 0u || !key_valid(private_key) ||
        !key_valid(peer_key) || allowed_mask == 0u) return -1;
    len = str_len(endpoint);
    if (len >= sizeof(g_wg.endpoint)) return -1;
    memset(&g_wg, 0, sizeof(g_wg));
    str_ncpy(g_wg.endpoint, endpoint, sizeof(g_wg.endpoint) - 1u);
    memcpy(g_wg.private_key, private_key, 32u);
    memcpy(g_wg.peer_key, peer_key, 32u);
    g_wg.endpoint_port = port;
    g_wg.allowed_network = allowed_network & allowed_mask;
    g_wg.allowed_mask = allowed_mask;
    g_wg.configured = 1u;
    return 0;
}

void wireguard_clear(void) {
    /* SECURITY: volatile write ensures the compiler does not optimize away
     * the key erasure even though g_wg is not used afterward. */
    volatile u8* p = (volatile u8*)&g_wg;
    for (u32 i = 0u; i < sizeof(g_wg); i++) p[i] = 0u;
}

int wireguard_start(void) {
    /* Routes stay untouched until handshake and replay protection exist. */
    if (!g_wg.configured) return -1;
    return -2;
}

int wireguard_stop(void) {
    if (!g_wg.active) return -1;
    g_wg.active = 0u;
    return 0;
}

void wireguard_status(wireguard_status_t* out) {
    if (out) memcpy(out, &g_wg, sizeof(*out));
}
