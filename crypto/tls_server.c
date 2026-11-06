#include "tls.h"
#include "kernel_api.h"
#include "timer.h"
#include "string.h"
#include "bearssl.h"
#include "tls_identity.h"

#define TLS_SERVER_BUFFER 16384u
#define TLS_SERVER_TIMEOUT 1000u

typedef struct { u32 socket_id; } tls_server_transport_t;
static br_ssl_server_context g_server;
static br_sslio_context g_io;
static tls_server_transport_t g_transport;
static u8 g_server_buffer[TLS_SERVER_BUFFER];
static u32 g_listener;
static u32 g_client;
static u16 g_port;
static u8 g_active;

static int server_read(void* context, unsigned char* data, size_t len) {
    tls_server_transport_t* t = (tls_server_transport_t*)context;
    u32 deadline = kticks() + TLS_SERVER_TIMEOUT;
    if (!t || !data || len == 0u) return -1;
    if (len > 1024u) len = 1024u;
    while ((i32)(kticks() - deadline) < 0) {
        int n;
        knetstack_poll(16u);
        n = knetstack_socket_recv(t->socket_id, data, (u16)len, 0, 0);
        if (n > 0) return n;
    }
    return -1;
}

static int server_write(void* context, const unsigned char* data, size_t len) {
    tls_server_transport_t* t = (tls_server_transport_t*)context;
    if (!t || (!data && len)) return -1;
    while (len) {
        u16 n = len > 1024u ? 1024u : (u16)len;
        if (knetstack_socket_send(t->socket_id, data, n) != 0) return -1;
        data += n; len -= n;
        knetstack_poll(8u);
    }
    return 0;
}

int tls_server_start(u16 port) {
    if (g_active || port == 0u) return -1;
    int listener = knetstack_socket_listen(port);
    if (listener < 0) return -1;
    g_listener = (u32)listener;
    g_client = 0u;
    g_port = port;
    g_active = 1u;
    return 0;
}

int tls_server_poll(void) {
    if (!g_active) return -1;
    if (!g_client) {
        u32 client = 0u;
        if (knetstack_socket_accept(g_listener, &client, 0, 0) != 0) return 0;
        g_client = client;
        g_transport.socket_id = client;
        u32 chain_len = 0u;
        const br_x509_certificate* chain = tls_identity_chain(&chain_len);
        const br_rsa_private_key* key = tls_identity_key();
        if (!chain || !key) {
            (void)knetstack_socket_close(g_client);
            g_client = 0u;
            return -1;
        }
        br_ssl_server_init_minr2g(&g_server, chain, chain_len, key);
        br_ssl_server_reset(&g_server);
        br_ssl_engine_set_buffer(&g_server.eng, g_server_buffer, sizeof(g_server_buffer), 1);
        br_sslio_init(&g_io, &g_server.eng, server_read, &g_transport, server_write, &g_transport);
    }
    {
        u8 request[512];
        int n = br_sslio_read(&g_io, request, sizeof(request) - 1u);
        if (n < 0) {
            (void)knetstack_socket_close(g_client);
            g_client = 0u;
            return -1;
        }
        if (n > 0) {
            static const char response[] =
                "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n"
                "Content-Length: 17\r\n\r\nAxiom TLS server\n";
            if (br_sslio_write_all(&g_io, response, sizeof(response) - 1u) != 0 || br_sslio_flush(&g_io) != 0) {
                (void)knetstack_socket_close(g_client);
                g_client = 0u;
                return -1;
            }
            (void)knetstack_socket_close(g_client);
            g_client = 0u;
        }
    }
    return 0;
}

int tls_server_stop(void) {
    if (!g_active) return -1;
    if (g_client) (void)knetstack_socket_close(g_client);
    (void)knetstack_socket_close(g_listener);
    g_client = 0u; g_listener = 0u; g_port = 0u; g_active = 0u;
    return 0;
}

int tls_server_active(void) { return g_active ? 1 : 0; }
