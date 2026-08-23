#include "ipv6.h"
#include "checksum.h"
#include "ip.h"

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int ipv6_parse(const char* text, u8 out_addr[16]) {
    u16 left[8], right[8];
    u32 left_count = 0u, right_count = 0u, i = 0u;
    int compressed = 0;
    if (!text || !out_addr || !text[0]) return -1;
    if (text[0] == ':' && text[1] == ':') { compressed = 1; i = 2u; }
    while (text[i] && text[i] != ':') {
        u32 value = 0u; u32 digits = 0u;
        while (text[i] && text[i] != ':' && digits < 4u) {
            int h = hex_value(text[i++]); if (h < 0) return -1;
            value = (value << 4u) | (u32)h; digits++;
        }
        if (!digits || left_count >= 8u) return -1;
        left[left_count++] = (u16)value;
        if (text[i] == ':') {
            if (text[i + 1u] == ':') { compressed = 1; i += 2u; break; }
            i++;
        }
    }
    if (compressed) {
        while (text[i]) {
            u32 value = 0u; u32 digits = 0u;
            while (text[i] && text[i] != ':' && digits < 4u) {
                int h = hex_value(text[i++]); if (h < 0) return -1;
                value = (value << 4u) | (u32)h; digits++;
            }
            if (!digits || right_count >= 8u) return -1;
            right[right_count++] = (u16)value;
            if (text[i] == ':') { if (!text[i + 1u]) return -1; i++; }
        }
    }
    if (!compressed && left_count != 8u) return -1;
    if (compressed && left_count + right_count >= 8u) return -1;
    for (u32 j = 0u; j < 16u; j++) out_addr[j] = 0u;
    for (u32 j = 0u; j < left_count; j++) { out_addr[j * 2u] = (u8)(left[j] >> 8u); out_addr[j * 2u + 1u] = (u8)left[j]; }
    u32 right_start = compressed ? 8u - right_count : left_count;
    for (u32 j = 0u; j < right_count; j++) { u32 p = right_start + j; out_addr[p * 2u] = (u8)(right[j] >> 8u); out_addr[p * 2u + 1u] = (u8)right[j]; }
    return 0;
}

int ipv6_link_local_from_mac(const u8* mac, u8 out_addr[16]) {
    if (!mac || !out_addr) return -1;
    out_addr[0] = 0xFEu; out_addr[1] = 0x80u;
    for (u32 i = 2u; i < 16u; i++) out_addr[i] = 0u;
    out_addr[8] = (u8)(mac[0] ^ 0x02u); out_addr[9] = mac[1]; out_addr[10] = mac[2];
    out_addr[11] = 0xFFu; out_addr[12] = 0xFEu;
    out_addr[13] = mac[3]; out_addr[14] = mac[4]; out_addr[15] = mac[5];
    return 0;
}

void ipv6_format(const u8 addr[16], char* out, u32 out_len) {
    static const char hex[] = "0123456789abcdef";
    u32 pos = 0u;
    if (!addr || !out || out_len == 0u) return;
    for (u32 i = 0u; i < 16u && pos + 5u < out_len; i += 2u) {
        if (i != 0u) out[pos++] = ':';
        out[pos++] = hex[(addr[i] >> 4u) & 0x0Fu];
        out[pos++] = hex[addr[i] & 0x0Fu];
        out[pos++] = hex[(addr[i + 1u] >> 4u) & 0x0Fu];
        out[pos++] = hex[addr[i + 1u] & 0x0Fu];
    }
    out[pos < out_len ? pos : out_len - 1u] = '\0';
}

int ipv6_build_header(ipv6_header_t* header, const u8 src[16], const u8 dst[16],
                      u8 next_header, u16 payload_len, u8 hop_limit) {
    if (!header || !src || !dst) return -1;
    header->version_class_flow = ip_htonl(0x60000000u);
    header->payload_len = ip_htons(payload_len);
    header->next_header = next_header;
    header->hop_limit = hop_limit;
    for (u32 i = 0u; i < 16u; i++) {
        header->src[i] = src[i];
        header->dst[i] = dst[i];
    }
    return 0;
}

u16 ipv6_transport_checksum(const u8 src[16], const u8 dst[16], u8 next_header,
                            const u8* payload, u16 payload_len) {
    u8 pseudo[40];
    u32 sum = 0u;
    if (!src || !dst || (!payload && payload_len != 0u)) return 0u;
    for (u32 i = 0u; i < 16u; i++) {
        pseudo[i] = src[i];
        pseudo[16u + i] = dst[i];
    }
    pseudo[32] = 0u; pseudo[33] = 0u; pseudo[34] = (u8)(payload_len >> 8u); pseudo[35] = (u8)payload_len;
    pseudo[36] = 0u; pseudo[37] = 0u; pseudo[38] = 0u; pseudo[39] = next_header;
    for (u32 i = 0u; i < sizeof(pseudo); i += 2u) sum += ((u32)pseudo[i] << 8u) | pseudo[i + 1u];
    for (u32 i = 0u; i + 1u < payload_len; i += 2u) sum += ((u32)payload[i] << 8u) | payload[i + 1u];
    if (payload_len & 1u) sum += (u32)payload[payload_len - 1u] << 8u;
    while (sum >> 16u) sum = (sum & 0xFFFFu) + (sum >> 16u);
    return ip_htons((u16)~sum);
}
