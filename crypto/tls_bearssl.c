#include "tls.h"
#include "kernel_api.h"
#include "string.h"
#include "timer.h"
#include "netstack.h"
#include "bearssl.h"

#define TLS_IO_BUFFER 16384u
#define TLS_IDLE_TIMEOUT 1000u

typedef struct {
    u32 socket_id;
} tls_transport_t;

static br_ssl_client_context g_ssl;
static br_x509_minimal_context g_x509;
static br_sslio_context g_io;
static u8 g_ssl_buffer[TLS_IO_BUFFER];

static void tls_inject_entropy(u32 socket_id, const char* host) {
    u8 entropy[32];
    u32 state = socket_id * 0x9E3779B9u;
    u32 i;
    kentropy_fill(entropy, sizeof(entropy));
    for (i = 0; i < 32u; ++i) {
        if (host && host[i % 32u]) state ^= (u32)(u8)host[i % 32u] << ((i & 3u) * 8u);
        entropy[i] ^= (u8)(state >> ((i & 3u) * 8u));
    }
    br_ssl_engine_inject_entropy(&g_ssl.eng, entropy, sizeof(entropy));
}

static int tls_read(void* context, unsigned char* data, size_t len) {
    tls_transport_t* transport = (tls_transport_t*)context;
    u32 deadline = kticks() + TLS_IDLE_TIMEOUT;
    if (!transport || !data || len == 0u) return -1;
    if (len > 1024u) len = 1024u;
    while ((i32)(kticks() - deadline) < 0) {
        int n;
        knetstack_poll(16u);
        n = knetstack_socket_recv(transport->socket_id, data, (u16)len, 0, 0);
        if (n > 0) return n;
        deadline = kticks() + TLS_IDLE_TIMEOUT;
    }
    return -1;
}

static int tls_write(void* context, const unsigned char* data, size_t len) {
    tls_transport_t* transport = (tls_transport_t*)context;
    if (!transport || (!data && len)) return -1;
    while (len) {
        u16 chunk = len > 1024u ? 1024u : (u16)len;
        u32 deadline = kticks() + TLS_IDLE_TIMEOUT;
        while (knetstack_socket_send(transport->socket_id, data, chunk) != 0) {
            u8 state = 0u;
            knetstack_poll(8u);
            if (knetstack_socket_state(transport->socket_id, &state) != 0 || state == NET_SOCKET_CLOSING || state == 0u || (i32)(kticks() - deadline) >= 0) return -1;
        }
        data += chunk;
        len -= chunk;
        knetstack_poll(8u);
    }
    return 0;
}

static int tls_stream_body(tls_body_sink_fn sink, void* context) {
    u8 data[1024];
    for (;;) {
        int n = br_sslio_read(&g_io, data, sizeof(data));
        if (n < 0) return -1;
        if (n == 0) return 0;
        if (sink && sink(data, (u32)n, context) != 0) return -1;
    }
}

int tls_available(void) {
    return 1;
}

int tls_http_get(u32 socket_id, const char* host, const char* path,
                 tls_body_sink_fn sink, void* context) {
    tls_transport_t transport;
    char request[512];
    size_t host_len, path_len;

    if (!host || !path) return -1;
    host_len = strlen(host);
    path_len = strlen(path);
    if (host_len == 0u || host_len >= 256u || path_len == 0u || path_len >= 256u) return -1;

    /* tls_profile.c supplies the pinned platform trust anchor when the
       caller passes an empty table; certificate validation remains enabled. */
    br_ssl_client_init_full(&g_ssl, &g_x509, 0, 0u);
    {
        rtc_time_t now;
        if (krtc_get_time(&now) == 0) {
            br_x509_minimal_set_time(&g_x509,
                krtc_days_since_epoch(&now), krtc_seconds_since_midnight(&now));
        } else {
            br_x509_minimal_set_time(&g_x509, 739815u, 0u);
        }
    }
    br_ssl_client_reset(&g_ssl, host, 0);
    tls_inject_entropy(socket_id, host);
    br_ssl_engine_set_buffer(&g_ssl.eng, g_ssl_buffer, sizeof(g_ssl_buffer), 1);
    transport.socket_id = socket_id;
    br_sslio_init(&g_io, &g_ssl.eng, tls_read, &transport, tls_write, &transport);

    str_cpy(request, "GET ");
    str_ncat(request, path, sizeof(request));
    str_ncat(request, " HTTP/1.0\r\nHost: ", sizeof(request));
    str_ncat(request, host, sizeof(request));
    str_ncat(request, "\r\nConnection: close\r\n\r\n", sizeof(request));
    if (br_sslio_write_all(&g_io, request, strlen(request)) != 0 || br_sslio_flush(&g_io) != 0) return -1;
    return tls_stream_body(sink, context);
}
