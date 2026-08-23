#ifndef LASTOS_FS_ICONV_H
#define LASTOS_FS_ICONV_H

#include "stdint.h"
#include "stddef.h"

typedef void *iconv_t;

static inline iconv_t iconv_open(const char *tocode, const char *fromcode) {
    (void)tocode; (void)fromcode;
    return (iconv_t)(uintptr_t)1;
}

static inline size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
                           char **outbuf, size_t *outbytesleft) {
    (void)cd;
    if (!inbuf || !*inbuf) return 0;
    size_t to_copy = (*inbytesleft < *outbytesleft) ? *inbytesleft : *outbytesleft;
    for (size_t i = 0; i < to_copy; i++) {
        (*outbuf)[i] = (*inbuf)[i];
    }
    *inbuf += to_copy;
    *inbytesleft -= to_copy;
    *outbuf += to_copy;
    *outbytesleft -= to_copy;
    return to_copy;
}

static inline int iconv_close(iconv_t cd) {
    (void)cd;
    return 0;
}

#endif /* LASTOS_FS_ICONV_H */
