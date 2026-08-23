#ifndef LASTOS_TLS_H
#define LASTOS_TLS_H

#include "types.h"

typedef int (*tls_body_sink_fn)(const u8* data, u32 len, void* context);

/* Establish TLS 1.2 over an already connected TCP socket and stream HTTP. */
int tls_http_get(u32 socket_id, const char* host, const char* path,
                 tls_body_sink_fn sink, void* context);
int tls_available(void);
int tls_server_start(u16 port);
int tls_server_poll(void);
int tls_server_stop(void);
int tls_server_active(void);

#endif
