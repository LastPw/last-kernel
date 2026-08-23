#include "banner.h"

const char* banner_guess_service(u16 port) {
    if (port == 21) return "ftp";
    if (port == 22) return "ssh";
    if (port == 23) return "telnet";
    if (port == 25) return "smtp";
    if (port == 53) return "dns";
    if (port == 80) return "http";
    if (port == 110) return "pop3";
    if (port == 143) return "imap";
    if (port == 443) return "https";
    if (port == 3306) return "mysql";
    if (port == 3389) return "rdp";
    return "unknown";
}

void banner_on_open_port(u32 target_ip, u16 port) {
    (void)target_ip;
    (void)port;
    /* Placeholder for active banner grabbing via TCP data exchange. */
}
