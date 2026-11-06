/* ==============================================================================
 * LastKernel Enterprise Server Edition — ONC RPC 2.0 & XDR Serialization Engine
 * RFC 1831 (RPC) & RFC 4506 (XDR: External Data Representation Standard)
 * ============================================================================== */

#include "nfs4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── XDR Stream Buffer Context ── */
typedef struct {
    u8* data;
    size_t size;
    size_t pos;
} xdr_stream_t;

void xdr_init_read(xdr_stream_t* xdr, const u8* buf, size_t len) {
    xdr->data = (u8*)buf;
    xdr->size = len;
    xdr->pos = 0;
}

void xdr_init_write(xdr_stream_t* xdr, u8* buf, size_t max_len) {
    xdr->data = buf;
    xdr->size = max_len;
    xdr->pos = 0;
}

/* ── XDR Primitive Encoding / Decoding ── */
int xdr_get_u32(xdr_stream_t* xdr, u32* out_val) {
    if (xdr->pos + 4 > xdr->size) return -1;
    *out_val = ((u32)xdr->data[xdr->pos] << 24) |
               ((u32)xdr->data[xdr->pos + 1] << 16) |
               ((u32)xdr->data[xdr->pos + 2] << 8) |
               ((u32)xdr->data[xdr->pos + 3]);
    xdr->pos += 4;
    return 0;
}

int xdr_put_u32(xdr_stream_t* xdr, u32 val) {
    if (xdr->pos + 4 > xdr->size) return -1;
    xdr->data[xdr->pos++] = (u8)(val >> 24);
    xdr->data[xdr->pos++] = (u8)(val >> 16);
    xdr->data[xdr->pos++] = (u8)(val >> 8);
    xdr->data[xdr->pos++] = (u8)(val);
    return 0;
}

int xdr_get_u64(xdr_stream_t* xdr, u64* out_val) {
    u32 hi, lo;
    if (xdr_get_u32(xdr, &hi) != 0 || xdr_get_u32(xdr, &lo) != 0) return -1;
    *out_val = ((u64)hi << 32) | (u64)lo;
    return 0;
}

int xdr_put_u64(xdr_stream_t* xdr, u64 val) {
    u32 hi = (u32)(val >> 32);
    u32 lo = (u32)(val & 0xFFFFFFFFULL);
    if (xdr_put_u32(xdr, hi) != 0 || xdr_put_u32(xdr, lo) != 0) return -1;
    return 0;
}

int xdr_get_opaque(xdr_stream_t* xdr, u8* out_buf, u32 len) {
    size_t padded = (len + 3) & ~3;
    if (xdr->pos + padded > xdr->size) return -1;
    if (out_buf) {
        memcpy(out_buf, xdr->data + xdr->pos, len);
    }
    xdr->pos += padded;
    return 0;
}

int xdr_put_opaque(xdr_stream_t* xdr, const u8* buf, u32 len) {
    size_t padded = (len + 3) & ~3;
    if (xdr->pos + padded > xdr->size) return -1;
    memcpy(xdr->data + xdr->pos, buf, len);
    /* Pad with zeros */
    for (size_t i = len; i < padded; i++) {
        xdr->data[xdr->pos + i] = 0;
    }
    xdr->pos += padded;
    return 0;
}

int xdr_get_string(xdr_stream_t* xdr, char* out_str, size_t max_str_len) {
    u32 len = 0;
    if (xdr_get_u32(xdr, &len) != 0) return -1;
    if (len >= max_str_len) return -2;
    if (xdr_get_opaque(xdr, (u8*)out_str, len) != 0) return -3;
    out_str[len] = '\0';
    return 0;
}

int xdr_put_string(xdr_stream_t* xdr, const char* str) {
    if (!str) str = "";
    u32 len = (u32)strlen(str);
    if (xdr_put_u32(xdr, len) != 0) return -1;
    return xdr_put_opaque(xdr, (const u8*)str, len);
}

/* ── ONC RPC 2.0 Header Structure (RFC 1831) ── */
typedef struct {
    u32 xid;
    u32 mtype;                 /* 0 = CALL, 1 = REPLY */
    u32 rpcvers;               /* 2 */
    u32 prog;                  /* 100003 for NFS */
    u32 vers;                  /* 4 */
    u32 proc;                  /* 1 for COMPOUND */
    u32 cred_flavor;           /* 0 = AUTH_NONE, 1 = AUTH_UNIX */
    u32 cred_len;
    u32 verf_flavor;
    u32 verf_len;
} rpc_msg_header_t;

int rpc_decode_call_header(xdr_stream_t* xdr, rpc_msg_header_t* hdr) {
    if (xdr_get_u32(xdr, &hdr->xid) != 0 ||
        xdr_get_u32(xdr, &hdr->mtype) != 0 ||
        xdr_get_u32(xdr, &hdr->rpcvers) != 0 ||
        xdr_get_u32(xdr, &hdr->prog) != 0 ||
        xdr_get_u32(xdr, &hdr->vers) != 0 ||
        xdr_get_u32(xdr, &hdr->proc) != 0) {
        return -1;
    }
    /* Skip Credentials & Verifier for AUTH_NONE / AUTH_SYS */
    if (xdr_get_u32(xdr, &hdr->cred_flavor) != 0 ||
        xdr_get_u32(xdr, &hdr->cred_len) != 0 ||
        xdr_get_opaque(xdr, NULL, hdr->cred_len) != 0 ||
        xdr_get_u32(xdr, &hdr->verf_flavor) != 0 ||
        xdr_get_u32(xdr, &hdr->verf_len) != 0 ||
        xdr_get_opaque(xdr, NULL, hdr->verf_len) != 0) {
        return -2;
    }
    return 0;
}

int rpc_encode_reply_header(xdr_stream_t* xdr, u32 xid, u32 accept_stat) {
    if (xdr_put_u32(xdr, xid) != 0 ||
        xdr_put_u32(xdr, 1) != 0 ||       /* 1 = REPLY */
        xdr_put_u32(xdr, 0) != 0 ||       /* 0 = MSG_ACCEPTED */
        xdr_put_u32(xdr, 0) != 0 ||       /* verf_flavor: AUTH_NONE */
        xdr_put_u32(xdr, 0) != 0 ||       /* verf_len: 0 */
        xdr_put_u32(xdr, accept_stat) != 0) { /* 0 = SUCCESS */
        return -1;
    }
    return 0;
}
