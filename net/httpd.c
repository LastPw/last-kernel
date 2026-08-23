#include "httpd.h"
#include "kernel_api.h"
#include "netstack.h"
#include "ip.h"
#include "vfs.h"
#include "string.h"
#include "bearssl.h"
#include "tls_identity.h"

#define HTTPD_MAX_HEADER 2048u
#define HTTPD_MAX_PATH 128u
#define HTTPD_MAX_ROOT 64u
#define HTTPD_IO_CHUNK 512u
#define HTTPD_TIMEOUT 300u
#define HTTPD_STATE_REQUEST 1u
#define HTTPD_STATE_STATIC 2u
#define HTTPD_STATE_PROXY_CONNECT 3u
#define HTTPD_STATE_PROXY_SEND 4u
#define HTTPD_STATE_PROXY_RECV 5u

typedef struct {
    u8 active;
    u8 state;
    u8 proxy;
    u8 header_done;
    u8 secure;
    u32 client;
    u32 upstream;
    u32 request_len;
    u32 file_offset;
    u32 file_size;
    u32 deadline;
    char request[HTTPD_MAX_HEADER];
    char path[HTTPD_MAX_PATH];
    char file_path[VFS_PATH_MAX];
} httpd_client_t;

static u8 g_active;
static u16 g_port;
static u32 g_listener;
static u32 g_tls_listener;
static u32 g_requests;
static u32 g_errors;
static char g_root[HTTPD_MAX_ROOT];
static char g_proxy_prefix[HTTPD_MAX_PATH];
static u32 g_proxy_ip;
static u16 g_proxy_port;
static u8 g_proxy_enabled;
static u8 g_tls_active;
static httpd_client_t g_client;
static br_ssl_server_context g_tls_server;
static br_sslio_context g_tls_io;
static u8 g_tls_buffer[16384];

typedef struct { u32 socket_id; } httpd_tls_transport_t;
static httpd_tls_transport_t g_tls_transport;

static u32 h_len(const char* s) { return s ? str_len(s) : 0u; }

static int h_has(const char* text, u32 len, const char* needle) {
    u32 n = h_len(needle);
    if (!text || !needle || n == 0u || n > len) return 0;
    for (u32 i = 0u; i + n <= len; i++) if (str_ncmp(text + i, needle, n) == 0) return 1;
    return 0;
}

static int h_prefix(const char* text, const char* prefix) {
    u32 n = h_len(prefix);
    return text && prefix && n && str_ncmp(text, prefix, n) == 0;
}

static void h_close_client(void) {
    if (g_client.upstream) (void)netstack_socket_close(g_client.upstream);
    if (g_client.client) (void)netstack_socket_close(g_client.client);
    memset(&g_client, 0, sizeof(g_client));
}

static int h_tls_read(void* context, unsigned char* data, size_t len) {
    httpd_tls_transport_t* t = (httpd_tls_transport_t*)context;
    u32 deadline = kticks() + HTTPD_TIMEOUT;
    if (!t || !data || len == 0u) return -1;
    if (len > HTTPD_IO_CHUNK) len = HTTPD_IO_CHUNK;
    while ((i32)(kticks() - deadline) < 0) {
        int n;
        netstack_poll(8u);
        n = netstack_socket_recv(t->socket_id, data, (u16)len, 0, 0);
        if (n > 0) return n;
    }
    return -1;
}

/* TCP keeps one unacknowledged segment per socket. Wait for that segment to
 * drain instead of dropping a later HTTP chunk when the peer is slow. */
static int h_socket_send_retry(u32 socket_id, const u8* data, u16 len) {
    u32 deadline = kticks() + HTTPD_TIMEOUT;
    if (!data && len) return -1;
    while ((i32)(kticks() - deadline) < 0) {
        u8 state = 0u;
        if (netstack_socket_send(socket_id, data, len) == 0) return 0;
        netstack_poll(8u);
        if (netstack_socket_state(socket_id, &state) != 0 || state == NET_SOCKET_CLOSING || state == 0u) return -1;
    }
    return -1;
}

static int h_tls_write(void* context, const unsigned char* data, size_t len) {
    httpd_tls_transport_t* t = (httpd_tls_transport_t*)context;
    if (!t || (!data && len)) return -1;
    while (len) {
        u16 part = len > HTTPD_IO_CHUNK ? HTTPD_IO_CHUNK : (u16)len;
        if (h_socket_send_retry(t->socket_id, data, part) != 0) return -1;
        data += part; len -= part;
        netstack_poll(4u);
    }
    return 0;
}

static int h_recv(u8* out, u16 len) {
    if (g_client.secure) return br_sslio_read(&g_tls_io, out, len);
    return netstack_socket_recv(g_client.client, out, len, 0, 0);
}

static int h_send(u32 sock, const char* text) {
    u32 len = h_len(text);
    while (len) {
        u16 part = len > HTTPD_IO_CHUNK ? HTTPD_IO_CHUNK : (u16)len;
        if (g_client.secure && sock == g_client.client) {
            if (br_sslio_write_all(&g_tls_io, text, part) != 0 || br_sslio_flush(&g_tls_io) != 0) return -1;
        } else if (h_socket_send_retry(sock, (const u8*)text, part) != 0) return -1;
        text += part;
        len -= part;
    }
    return 0;
}

static int h_error(const char* code, const char* message) {
    char response[256];
    u32 body_len = h_len(message);
    u32 p = 0u;
    static const char prefix[] = "HTTP/1.1 ";
    static const char middle[] = "\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: ";
    if (!g_client.client || body_len > 96u) return -1;
    memset(response, 0, sizeof(response));
    str_cpy(response, prefix); p = h_len(response);
    str_ncpy(response + p, code, 32u); p += h_len(code);
    str_cpy(response + p, middle); p += h_len(middle);
    response[p++] = (char)('0' + (char)((body_len / 10u) % 10u));
    response[p++] = (char)('0' + (char)(body_len % 10u));
    str_cpy(response + p, "\r\n\r\n"); p += 4u;
    if (p + body_len + 1u >= sizeof(response)) return -1;
    memcpy(response + p, message, body_len); p += body_len;
    response[p] = '\0';
    if (h_send(g_client.client, response) != 0) return -1;
    g_errors++;
    h_close_client();
    return 0;
}

static int h_path_safe(const char* path) {
    if (!path || path[0] != '/' || h_len(path) >= HTTPD_MAX_PATH) return 0;
    if (h_has(path, h_len(path), "..")) return 0;
    for (u32 i = 0u; path[i]; i++) if ((u8)path[i] < 0x20u) return 0;
    return 1;
}

static int h_make_file_path(const char* url, char* out, u32 out_len) {
    u32 p = 0u;
    if (!url || !out || out_len < 4u || !h_path_safe(url)) return -1;
    str_cpy(out, g_root);
    p = h_len(out);
    if (p && out[p - 1u] == '/' && url[0] == '/') url++;
    if (p + h_len(url) + 1u >= out_len) return -1;
    str_cpy(out + p, url);
    p = h_len(out);
    if (p && out[p - 1u] == '/') {
        if (p + 10u >= out_len) return -1;
        str_cpy(out + p, "index.html");
    }
    return 0;
}

static int h_parse_request(void) {
    char method[8];
    u32 i = 0u, line_end = 0u, path_len = 0u;
    u8 range_requested = 0u;
    u32 range_start = 0u;
    if (!h_has(g_client.request, g_client.request_len, "\r\n\r\n")) return 0;
    g_client.header_done = 1u;
    while (i + 1u < g_client.request_len && !(g_client.request[i] == '\r' && g_client.request[i + 1u] == '\n')) i++;
    if (i == 0u || i >= g_client.request_len || i >= sizeof(method)) { (void)h_error("400 Bad Request", "bad request\n"); return -1; }
    memset(method, 0, sizeof(method));
    for (u32 j = 0u; j < i && j + 1u < sizeof(method); j++) {
        if (g_client.request[j] == ' ') { method[j] = '\0'; break; }
        method[j] = g_client.request[j];
    }
    if (str_cmp(method, "GET") != 0 && str_cmp(method, "HEAD") != 0) {
        (void)h_error("405 Method Not Allowed", "GET and HEAD only\n"); return -1;
    }
    while (line_end + 1u < i && g_client.request[line_end] != ' ') line_end++;
    if (line_end >= i) { (void)h_error("400 Bad Request", "bad target\n"); return -1; }
    line_end++;
    while (line_end < i && g_client.request[line_end] == ' ') line_end++;
    while (line_end + path_len < i && g_client.request[line_end + path_len] != ' ') path_len++;
    if (path_len == 0u || path_len >= sizeof(g_client.path)) { (void)h_error("414 URI Too Long", "path too long\n"); return -1; }
    for (u32 j = 0u; j < path_len; j++) g_client.path[j] = g_client.request[line_end + j];
    g_client.path[path_len] = '\0';
    for (u32 j = 0u; g_client.path[j]; j++) if (g_client.path[j] == '?') { g_client.path[j] = '\0'; break; }
    if (!h_path_safe(g_client.path)) { (void)h_error("403 Forbidden", "unsafe path\n"); return -1; }
    g_client.proxy = g_proxy_enabled && h_prefix(g_client.path, g_proxy_prefix);
    if (g_client.proxy) {
        u16 local = (u16)(40000u + (kticks() & 0x0FFFu));
        int sock = netstack_socket_open(IP_PROTO_TCP, local, g_proxy_ip, g_proxy_port);
        if (sock < 0) { (void)h_error("502 Bad Gateway", "upstream unavailable\n"); return -1; }
        g_client.upstream = (u32)sock;
        g_client.state = HTTPD_STATE_PROXY_CONNECT;
        g_client.deadline = kticks() + HTTPD_TIMEOUT;
        return 1;
    }
    if (h_make_file_path(g_client.path, g_client.file_path, sizeof(g_client.file_path)) != 0) {
        (void)h_error("400 Bad Request", "invalid path\n"); return -1;
    }
    {
        vfs_node_info_t info;
        if (vfs_info(g_client.file_path, &info) != 0 || info.type != VFS_NODE_FILE || vfs_access(g_client.file_path, VFS_ACC_READ) == 0) {
            (void)h_error("404 Not Found", "not found\n"); return -1;
        }
        g_client.file_size = info.size;
    }
    /* Accept the single-range form used by wget -c: Range: bytes=N-. */
    for (u32 r = 0u; r + 14u < g_client.request_len; r++) {
        if (str_ncmp(g_client.request + r, "Range: bytes=", 13u) == 0 || str_ncmp(g_client.request + r, "range: bytes=", 13u) == 0) {
            u32 value = 0u, digits = 0u;
            r += 13u;
            while (g_client.request[r] >= '0' && g_client.request[r] <= '9') {
                value = value > 0x0FFFFFFFu ? 0xFFFFFFFFu : value * 10u + (u32)(g_client.request[r] - '0');
                r++; digits++;
            }
            if (digits && g_client.request[r] == '-' && value < g_client.file_size) {
                range_requested = 1u;
                range_start = value;
            }
            break;
        }
    }
    if (range_requested) g_client.file_offset = range_start;
    {
        char header[384];
        u32 content_length = g_client.file_size - g_client.file_offset;
        str_cpy(header, range_requested ? "HTTP/1.1 206 Partial Content\r\nContent-Type: application/octet-stream\r\nConnection: close\r\nContent-Length: " : "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nConnection: close\r\nContent-Length: ");
        {
            char digits[12]; u32 value = content_length, d = 0u;
            do { digits[d++] = (char)('0' + (value % 10u)); value /= 10u; } while (value && d < sizeof(digits));
            while (d) { u32 at = h_len(header); header[at] = digits[--d]; header[at + 1u] = '\0'; }
        }
        if (range_requested) {
            char digits[12]; u32 value = g_client.file_size - 1u, d = 0u;
            str_ncat(header, "\r\nContent-Range: bytes ", sizeof(header));
            value = range_start; do { digits[d++] = (char)('0' + (value % 10u)); value /= 10u; } while (value && d < sizeof(digits));
            while (d) { u32 at = h_len(header); header[at] = digits[--d]; header[at + 1u] = '\0'; }
            str_ncat(header, "-", sizeof(header));
            value = g_client.file_size - 1u; d = 0u; do { digits[d++] = (char)('0' + (value % 10u)); value /= 10u; } while (value && d < sizeof(digits));
            while (d) { u32 at = h_len(header); header[at] = digits[--d]; header[at + 1u] = '\0'; }
            str_ncat(header, "/", sizeof(header));
            value = g_client.file_size; d = 0u; do { digits[d++] = (char)('0' + (value % 10u)); value /= 10u; } while (value && d < sizeof(digits));
            while (d) { u32 at = h_len(header); header[at] = digits[--d]; header[at + 1u] = '\0'; }
        }
        str_ncat(header, "\r\n\r\n", sizeof(header));
        if (h_send(g_client.client, header) != 0) { h_close_client(); return -1; }
    }
    g_client.state = HTTPD_STATE_STATIC;
    return 1;
}

static void h_static_poll(void) {
    char data[HTTPD_IO_CHUNK];
    int n;
    if (g_client.file_offset >= g_client.file_size) { h_close_client(); return; }
    n = vfs_read_file_at(g_client.file_path, g_client.file_offset, data, sizeof(data));
    if (n <= 0) { (void)h_error("500 Internal Server Error", "read failed\n"); return; }
    if (g_client.secure) {
        if (br_sslio_write_all(&g_tls_io, data, (size_t)n) != 0 || br_sslio_flush(&g_tls_io) != 0) { h_close_client(); return; }
    } else if (h_socket_send_retry(g_client.client, (const u8*)data, (u16)n) != 0) { h_close_client(); return; }
    g_client.file_offset += (u32)n;
}

static void h_proxy_poll(void) {
    u8 state = 0u;
    u8 data[HTTPD_IO_CHUNK];
    if (netstack_socket_state(g_client.upstream, &state) != 0) { (void)h_error("502 Bad Gateway", "upstream state failed\n"); return; }
    if (g_client.state == HTTPD_STATE_PROXY_CONNECT) {
        if (state == NET_SOCKET_ESTABLISHED) {
            if (h_send(g_client.upstream, g_client.request) != 0) { (void)h_error("502 Bad Gateway", "upstream write failed\n"); return; }
            g_client.state = HTTPD_STATE_PROXY_RECV;
        } else if ((i32)(kticks() - g_client.deadline) >= 0) {
            (void)h_error("504 Gateway Timeout", "upstream timeout\n");
        }
        return;
    }
    {
        int n = netstack_socket_recv(g_client.upstream, data, sizeof(data), 0, 0);
        if (n > 0) {
            if (h_socket_send_retry(g_client.client, data, (u16)n) != 0) h_close_client();
            return;
        }
        if (state == NET_SOCKET_CLOSING || state == 0u) h_close_client();
    }
}

int httpd_start(u16 port, const char* root) {
    int listener;
    if (g_active || g_client.active || port == 0u || !root || h_len(root) >= sizeof(g_root) || root[0] != '/') return -1;
    listener = netstack_socket_listen(port);
    if (listener < 0) return -1;
    str_cpy(g_root, root);
    g_port = port; g_listener = (u32)listener; g_active = 1u;
    g_requests = 0u; g_errors = 0u;
    memset(&g_client, 0, sizeof(g_client));
    return 0;
}

int httpd_start_tls(u16 port, const char* root) {
    int listener;
    if (g_tls_active || g_client.active || port == 0u || !root || h_len(root) >= sizeof(g_root) || root[0] != '/') return -1;
    listener = netstack_socket_listen(port);
    if (listener < 0) return -1;
    str_cpy(g_root, root);
    g_tls_listener = (u32)listener;
    g_tls_active = 1u;
    g_requests = 0u; g_errors = 0u;
    memset(&g_client, 0, sizeof(g_client));
    return 0;
}

int httpd_stop(void) {
    if (!g_active && !g_tls_active) return -1;
    h_close_client();
    if (g_listener) (void)netstack_socket_close(g_listener);
    if (g_tls_listener) (void)netstack_socket_close(g_tls_listener);
    g_listener = 0u; g_tls_listener = 0u; g_port = 0u; g_active = 0u; g_tls_active = 0u;
    return 0;
}

void httpd_poll(u32 budget) {
    (void)budget;
    if (!g_active && !g_tls_active) return;
    if (!g_client.active) {
        u32 client = 0u;
        u32 accepted_from = 0u;
        if (g_listener && netstack_socket_accept(g_listener, &client, 0, 0) == 0) accepted_from = g_listener;
        else if (g_tls_listener && netstack_socket_accept(g_tls_listener, &client, 0, 0) == 0) accepted_from = g_tls_listener;
        if (accepted_from) {
            memset(&g_client, 0, sizeof(g_client));
            g_client.active = 1u; g_client.state = HTTPD_STATE_REQUEST;
            g_client.client = client; g_client.deadline = kticks() + HTTPD_TIMEOUT;
            g_client.secure = accepted_from == g_tls_listener ? 1u : 0u;
            if (g_client.secure) {
                g_tls_transport.socket_id = client;
                u32 chain_len = 0u;
                const br_x509_certificate* chain = tls_identity_chain(&chain_len);
                const br_rsa_private_key* key = tls_identity_key();
                if (!chain || !key) {
                    h_close_client();
                    return;
                }
                br_ssl_server_init_minr2g(&g_tls_server, chain, chain_len, key);
                br_ssl_server_reset(&g_tls_server);
                br_ssl_engine_set_buffer(&g_tls_server.eng, g_tls_buffer, sizeof(g_tls_buffer), 1);
                br_sslio_init(&g_tls_io, &g_tls_server.eng, h_tls_read, &g_tls_transport,
                              h_tls_write, &g_tls_transport);
            }
            g_requests++;
        }
        return;
    }
    if (g_client.state == HTTPD_STATE_REQUEST) {
        u8 data[HTTPD_IO_CHUNK];
        int n = h_recv(data, sizeof(data));
        if (n > 0) {
            if (g_client.request_len + (u32)n >= sizeof(g_client.request)) { (void)h_error("413 Payload Too Large", "headers too large\n"); return; }
            memcpy(g_client.request + g_client.request_len, data, (u32)n);
            g_client.request_len += (u32)n; g_client.request[g_client.request_len] = '\0';
            g_client.deadline = kticks() + HTTPD_TIMEOUT;
            (void)h_parse_request();
        } else if ((i32)(kticks() - g_client.deadline) >= 0) { (void)h_error("408 Request Timeout", "request timeout\n"); }
    } else if (g_client.state == HTTPD_STATE_STATIC) h_static_poll();
    else if (g_client.state == HTTPD_STATE_PROXY_CONNECT || g_client.state == HTTPD_STATE_PROXY_RECV) h_proxy_poll();
}

int httpd_active(void) { return (g_active || g_tls_active) ? 1 : 0; }
int httpd_tls_active(void) { return g_tls_active ? 1 : 0; }

int httpd_set_proxy(const char* prefix, u32 ip, u16 port) {
    if (!prefix || prefix[0] != '/' || h_len(prefix) >= sizeof(g_proxy_prefix) || port == 0u) return -1;
    str_cpy(g_proxy_prefix, prefix); g_proxy_ip = ip; g_proxy_port = port; g_proxy_enabled = 1u;
    return 0;
}

void httpd_clear_proxy(void) { memset(g_proxy_prefix, 0, sizeof(g_proxy_prefix)); g_proxy_enabled = 0u; }

void httpd_status(u16* out_port, u32* out_requests, u32* out_errors, u8* out_proxy, char* out_root, u32 root_len) {
    if (out_port) *out_port = g_port;
    if (out_requests) *out_requests = g_requests;
    if (out_errors) *out_errors = g_errors;
    if (out_proxy) *out_proxy = g_proxy_enabled;
    if (out_root && root_len) str_ncpy(out_root, g_root, root_len - 1u);
}
