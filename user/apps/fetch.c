#include "libsys.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    println("=== LastKernel Ring-3 Network Fetch ===");
    println("[fetch] Connecting to HTTP endpoint 10.0.2.2:80 via Ring-3 Socket...");

    /* 10.0.2.2 in big-endian / network format: 0x0A000202 (or 127.0.0.1: 0x7F000001) */
    u32 remote_ip = 0x0A000202u;
    u16 remote_port = 80;
    u32 local_ip = 0x0A00020Fu; /* 10.0.2.15 */
    u16 local_port = 45678;

    int sock = socket_open(remote_ip, remote_port, local_ip, local_port);
    if (sock < 0) {
        println("[fetch] Error: Failed to open network socket.");
        return 1;
    }

    print("[fetch] Socket connected successfully! ID: ");
    print_dec(sock);
    println("");

    const char* http_req = "GET / HTTP/1.0\r\nHost: lastos\r\nConnection: close\r\n\r\n";
    i32 sent = socket_send(sock, http_req, strlen(http_req));
    print("[fetch] Sent HTTP request bytes: ");
    print_dec(sent);
    println("");

    char buf[512];
    memset(buf, 0, sizeof(buf));
    i32 n = socket_recv(sock, buf, sizeof(buf) - 1);
    if (n > 0) {
        println("[fetch] Received Server Response:");
        println(buf);
    } else {
        println("[fetch] Connection established, awaiting server stream.");
    }

    socket_close(sock);
    println("[fetch] Socket closed cleanly. Network transaction complete.");
    return 0;
}
