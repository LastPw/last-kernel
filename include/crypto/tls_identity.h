#ifndef LASTOS_TLS_IDENTITY_H
#define LASTOS_TLS_IDENTITY_H

#include "types.h"
#include "bearssl.h"

int tls_identity_init(void);
const br_x509_certificate* tls_identity_chain(u32* out_len);
const br_rsa_private_key* tls_identity_key(void);

#endif
