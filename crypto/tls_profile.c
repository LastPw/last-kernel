#include "bearssl.h"
#include "tls_trust.h"

/* A small client profile: modern TLS 1.2 suites only, with no legacy
   CBC/3DES/CCM code pulled into the kernel. */
void br_ssl_client_init_full(br_ssl_client_context* cc,
                             br_x509_minimal_context* xc,
                             const br_x509_trust_anchor* anchors,
                             size_t anchors_num) {
    static const uint16_t suites[] = {
        BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
        BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384
    };

    br_ssl_client_zero(cc);
    br_ssl_engine_set_versions(&cc->eng, BR_TLS12, BR_TLS12);
    if (!anchors || anchors_num == 0u) {
        anchors = lastos_tls_trust_anchors;
        anchors_num = lastos_tls_trust_anchor_count;
    }
    br_x509_minimal_init(xc, &br_sha256_vtable, anchors, anchors_num);
    br_ssl_engine_set_suites(&cc->eng, suites, sizeof(suites) / sizeof(suites[0]));
    br_ssl_client_set_default_rsapub(cc);
    br_ssl_engine_set_default_rsavrfy(&cc->eng);
    br_ssl_engine_set_default_ecdsa(&cc->eng);
    br_x509_minimal_set_rsa(xc, br_ssl_engine_get_rsavrfy(&cc->eng));
    br_x509_minimal_set_ecdsa(xc, br_ssl_engine_get_ec(&cc->eng),
                              br_ssl_engine_get_ecdsa(&cc->eng));

    br_ssl_engine_set_hash(&cc->eng, br_sha1_ID, &br_sha1_vtable);
    br_ssl_engine_set_hash(&cc->eng, br_sha256_ID, &br_sha256_vtable);
    br_ssl_engine_set_hash(&cc->eng, br_sha384_ID, &br_sha384_vtable);
    br_x509_minimal_set_hash(xc, br_sha1_ID, &br_sha1_vtable);
    br_x509_minimal_set_hash(xc, br_sha256_ID, &br_sha256_vtable);
    br_x509_minimal_set_hash(xc, br_sha384_ID, &br_sha384_vtable);
    br_ssl_engine_set_x509(&cc->eng, &xc->vtable);
    br_ssl_engine_set_prf10(&cc->eng, &br_tls10_prf);
    br_ssl_engine_set_prf_sha256(&cc->eng, &br_tls12_sha256_prf);
    br_ssl_engine_set_prf_sha384(&cc->eng, &br_tls12_sha384_prf);
    br_ssl_engine_set_default_aes_gcm(&cc->eng);
}
