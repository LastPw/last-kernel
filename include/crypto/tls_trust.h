#ifndef LASTOS_TLS_TRUST_H
#define LASTOS_TLS_TRUST_H

#include "bearssl.h"
#include "stddef.h"

extern const br_x509_trust_anchor lastos_tls_trust_anchors[];
extern const size_t lastos_tls_trust_anchor_count;

#endif
