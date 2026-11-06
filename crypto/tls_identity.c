#include "tls_identity.h"
#include "string.h"
#include "../build/tls_identity.h"

static br_skey_decoder_context g_decoder;
static br_rsa_private_key g_key;
static br_x509_certificate g_chain[1];
static u8 g_ready;

int tls_identity_init(void) {
    if (g_ready) return 0;
    br_skey_decoder_init(&g_decoder);
    br_skey_decoder_push(&g_decoder, LASTOS_TLS_KEY_DER, LASTOS_TLS_KEY_LEN);
    if (br_skey_decoder_last_error(&g_decoder) != 0 ||
        br_skey_decoder_key_type(&g_decoder) != BR_KEYTYPE_RSA) return -1;
    {
        const br_rsa_private_key* decoded = br_skey_decoder_get_rsa(&g_decoder);
        if (!decoded) return -1;
        memcpy(&g_key, decoded, sizeof(g_key));
    }
    g_chain[0].data = (unsigned char*)LASTOS_TLS_CERT_DER;
    g_chain[0].data_len = LASTOS_TLS_CERT_LEN;
    g_ready = 1u;
    return 0;
}

const br_x509_certificate* tls_identity_chain(u32* out_len) {
    if (tls_identity_init() != 0) return 0;
    if (out_len) *out_len = 1u;
    return g_chain;
}

const br_rsa_private_key* tls_identity_key(void) {
    return tls_identity_init() == 0 ? &g_key : 0;
}
