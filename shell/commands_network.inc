/* Network, HTTP, TLS, and diagnostics command handlers. */
#include "airmesh.h"

static void cmd_dns(const char* args) {
    char sub[16], slot_text[16], ip_text[32];
    u32 slot = 0u, ip = 0u;
    int ok = 0;
    args = read_token(skip_space(args), sub, sizeof(sub));
    if (!sub[0] || str_cmp(sub, "show") == 0) {
        for (u32 i = 0u; i < DNS_SERVER_MAX; i++) {
            char formatted[32];
            if (dns_get_server(i, &ip) != 0 || ip == 0u) continue;
            ip_format_ipv4(ip, formatted, sizeof(formatted));
            kputs("dns["); print_u32(i); kputs("]="); kputs(formatted); kputc('\n');
        }
        kputs("dns cache: use 'dns cache clear' to flush\n");
        return;
    }
    if (str_cmp(sub, "reset") == 0) {
        dns_reset_servers();
        kputs("dns servers reset to DHCP + public fallback\n");
        return;
    }
    if (str_cmp(sub, "cache") == 0) {
        args = read_token(skip_space(args), slot_text, sizeof(slot_text));
        if (str_cmp(slot_text, "clear") != 0) { kputs("Usage: dns cache clear\n"); return; }
        dns_cache_clear();
        kputs("dns cache cleared\n");
        return;
    }
    if (str_cmp(sub, "set") == 0) {
        args = read_token(skip_space(args), slot_text, sizeof(slot_text));
        args = read_token(skip_space(args), ip_text, sizeof(ip_text));
        if (parse_u32(slot_text, &slot) != 0 || slot >= DNS_SERVER_MAX) {
            kputs("Usage: dns set <slot 0..2> <ipv4>\n"); return;
        }
        ip = ip_parse_ipv4(ip_text, &ok);
        if (!ok || ip == 0u || dns_set_server(slot, ip) != 0) { kputs("dns: invalid server\n"); return; }
        kputs("dns server configured\n");
        return;
    }
    kputs("Usage: dns show|set <slot 0..2> <ipv4>|reset|cache clear\n");
}

static void cmd_netstack(const char* args) {
    char t0[16], t1[32], t2[32], t3[32], t4[16];
    u32 n = 8, idx = 0, metric = 0;
    int ok = 0;
    args = skip_space(args);

    if (!args[0] || str_cmp(args, "status") == 0) {
        netstack_status_t st;
        knetstack_status(&st);
        kputs("netstack frames/ip/arp/malformed: ");
        print_u32(st.frames_rx); kputs("/");
        print_u32(st.ipv4_rx); kputs("/");
        print_u32(st.arp_rx); kputs("/");
        print_u32(st.malformed);
        kputs("\narp/routes/sockets: ");
        print_u32(st.arp_entries); kputs("/");
        print_u32(st.route_entries); kputs("/");
        print_u32(st.sockets_open);
        kputc('\n');
        return;
    }

    if (starts_with(args, "arp")) {
        args += 3;
        args = skip_space(args);
        if (args[0]) {
            (void)read_token(args, t0, sizeof(t0));
            if (parse_u32(t0, &n) != 0) n = 8u;
        }
        if (n > 32u) n = 32u;
        for (u32 i = 0; i < n; i++) {
            net_arp_entry_t e;
            char ipb[32];
            if (knetstack_arp_get(i, &e) != 0) break;
            ip_format_ipv4(e.ip, ipb, sizeof(ipb));
            kputs("#"); print_u32(i);
            kputs(" ip="); kputs(ipb);
            kputs(" mac=");
            for (u32 m = 0; m < 6u; m++) {
                u8 v = e.mac[m];
                char hx[3];
                hx[0] = (char)((v >> 4u) < 10 ? ('0' + (v >> 4u)) : ('A' + (v >> 4u) - 10));
                hx[1] = (char)((v & 0xFu) < 10 ? ('0' + (v & 0xFu)) : ('A' + (v & 0xFu) - 10));
                hx[2] = '\0';
                kputs(hx);
                if (m + 1u < 6u) kputc(':');
            }
            kputs(" seen="); print_u32(e.last_seen_tick);
            kputc('\n');
        }
        return;
    }

    if (starts_with(args, "route add ")) {
        u32 net = 0, mask = 0, gw = 0;
        args += 10;
        args = read_token(args, t1, sizeof(t1));
        args = read_token(args, t2, sizeof(t2));
        args = read_token(args, t3, sizeof(t3));
        (void)read_token(args, t4, sizeof(t4));
        net = ip_parse_ipv4(t1, &ok); if (!ok) { kputs("bad net ip\n"); return; }
        mask = ip_parse_ipv4(t2, &ok); if (!ok) { kputs("bad mask ip\n"); return; }
        gw = ip_parse_ipv4(t3, &ok); if (!ok) { kputs("bad gateway ip\n"); return; }
        if (parse_u32(t4, &metric) != 0) metric = 100u;
        if (knetstack_route_add(net, mask, gw, metric) != 0) kputs("route add failed\n");
        else kputs("route added\n");
        return;
    }

    if (starts_with(args, "route")) {
        args += 5;
        args = skip_space(args);
        if (args[0]) {
            (void)read_token(args, t0, sizeof(t0));
            if (parse_u32(t0, &n) != 0) n = 8u;
        }
        if (n > 32u) n = 32u;
        for (u32 i = 0; i < n; i++) {
            net_route_entry_t r;
            char nb[32], mb[32], gb[32];
            if (knetstack_route_get(i, &r) != 0) break;
            ip_format_ipv4(r.network, nb, sizeof(nb));
            ip_format_ipv4(r.mask, mb, sizeof(mb));
            ip_format_ipv4(r.gateway, gb, sizeof(gb));
            kputs("#"); print_u32(i);
            kputs(" net/mask/gw=");
            kputs(nb); kputs("/"); kputs(mb); kputs(" via "); kputs(gb);
            kputs(" metric="); print_u32(r.metric);
            kputc('\n');
        }
        return;
    }

    if (starts_with(args, "sock open ")) {
        u32 proto = 0, lport = 0, rport = 0;
        u32 rip = 0;
        args += 10;
        args = read_token(args, t0, sizeof(t0));
        args = read_token(args, t1, sizeof(t1));
        args = read_token(args, t2, sizeof(t2));
        (void)read_token(args, t3, sizeof(t3));
        if (parse_u32(t1, &lport) != 0 || parse_u32(t3, &rport) != 0) {
            kputs("Usage: netstack sock open <tcp|udp> <lport> <rip> <rport>\n");
            return;
        }
        rip = ip_parse_ipv4(t2, &ok);
        if (!ok) {
            kputs("bad remote ip\n");
            return;
        }
        proto = (str_cmp(t0, "tcp") == 0) ? IP_PROTO_TCP : IP_PROTO_UDP;
        idx = (u32)knetstack_socket_open(proto, (u16)lport, rip, (u16)rport);
        if ((i32)idx < 0) kputs("socket open failed\n");
        else { kputs("socket id="); print_u32(idx); kputc('\n'); }
        return;
    }

    if (starts_with(args, "sock send ")) {
        char send_data[256];
        u32 send_len = 0u;
        args += 10;
        args = read_token(args, t0, sizeof(t0));
        args = skip_space(args);
        send_data[0] = '\0';
        if (args[0]) {
            (void)read_token(args, send_data, sizeof(send_data));
            send_len = str_len(send_data);
        }
        if (parse_u32(t0, &idx) != 0) {
            kputs("Usage: netstack sock send <id> [data]\n");
            return;
        }
        if (knetstack_socket_send(idx, send_len ? (const u8*)send_data : 0, (u16)send_len) != 0) kputs("socket send failed\n");
        else kputs("socket send ok\n");
        return;
    }

    if (starts_with(args, "sock recv ")) {
        u8 payload[256];
        u32 src_ip = 0u;
        u16 src_port = 0u;
        char ipb[32];
        args += 10;
        (void)read_token(args, t0, sizeof(t0));
        if (parse_u32(t0, &idx) != 0) {
            kputs("Usage: netstack sock recv <id>\n");
            return;
        }
        int nrecv = knetstack_socket_recv(idx, payload, (u16)sizeof(payload), &src_ip, &src_port);
        if (nrecv < 0) {
            kputs("socket receive queue empty\n");
            return;
        }
        ip_format_ipv4(src_ip, ipb, sizeof(ipb));
        kputs("received from "); kputs(ipb); kputs(":"); print_u32(src_port);
        kputs(" bytes="); print_u32((u32)nrecv); kputs(" data=");
        for (int i = 0; i < nrecv; i++) {
            u8 ch = payload[i];
            kputc((ch >= 32u && ch < 127u) ? (char)ch : '.');
        }
        kputc('\n');
        return;
    }

    if (starts_with(args, "sock close ")) {
        args += 11;
        (void)read_token(args, t0, sizeof(t0));
        if (parse_u32(t0, &idx) != 0) {
            kputs("Usage: netstack sock close <id>\n");
            return;
        }
        if (knetstack_socket_close(idx) != 0) kputs("socket close failed\n");
        else kputs("socket closed\n");
        return;
    }

    if (starts_with(args, "sock list")) {
        u32 c = knetstack_socket_count();
        kputs("sockets: "); print_u32(c); kputc('\n');
        for (u32 i = 0; i < c; i++) {
            net_socket_info_t s;
            char ipb[32];
            if (knetstack_socket_get(i, &s) != 0) break;
            ip_format_ipv4(s.remote_ip, ipb, sizeof(ipb));
            kputs("#"); print_u32(i);
            kputs(" id="); print_u32(s.id);
            kputs(" proto="); print_u32(s.proto);
            kputs(" l/r="); print_u32(s.local_port); kputs("/"); print_u32(s.remote_port);
            kputs(" rip="); kputs(ipb);
            kputs(" pending/dropped="); print_u32(s.rx_pending); kputs("/"); print_u32(s.rx_dropped);
            kputc('\n');
        }
        return;
    }

    kputs("Usage: netstack status|arp [n]|route [n]|route add <net> <mask> <gw> <metric>|sock open <tcp|udp> <lport> <rip> <rport>|sock send <id> [data]|sock recv <id>|sock close <id>|sock list\n");
}

static int shell_resolve_host(const char* host, u32* out_ip) {
    int ok = 0;
    if (!host || !out_ip) return -1;
    *out_ip = ip_parse_ipv4(host, &ok);
    if (ok) return 0;
    return dns_resolve_ipv4(host, out_ip, 200u);
}

static int shell_resolve_host6(const char* host, u8 out_ip[16]) {
    if (!host || !out_ip) return -1;
    if (ipv6_parse(host, out_ip) == 0) return 0;
    return dns_resolve_ipv6(host, out_ip, 200u);
}

static void shell_print_mac(const u8* mac, char separator) {
    if (!mac) return;
    for (u32 i = 0u; i < 6u; i++) {
        u8 v = mac[i];
        char hx[3];
        hx[0] = (char)(((v >> 4u) < 10u) ? ('0' + (v >> 4u)) : ('A' + (v >> 4u) - 10));
        hx[1] = (char)(((v & 0xFu) < 10u) ? ('0' + (v & 0xFu)) : ('A' + (v & 0xFu) - 10));
        hx[2] = '\0';
        kputs(hx);
        if (i != 5u) kputc(separator);
    }
}

static void cmd_ifconfig(const char* args) {
    netstack_status_t status;
    char ip[32];
    u8 mac[6];
    u8 link_local[16];
    (void)args;
    knetstack_status(&status);

    kputs("Axiom IP Configuration\n\n");
    kputs("Ethernet adapter rtl8139:\n\n");
    if (!nic_ready()) {
        kputs("   Media State . . . . . . . . . . : Media disconnected\n");
        return;
    }

    ip_format_ipv4(status.local_ip, ip, sizeof(ip));
    kputs("   IPv4 Address. . . . . . . . . . : "); kputs(ip); kputc('\n');
    ip_format_ipv4(status.local_mask, ip, sizeof(ip));
    kputs("   Subnet Mask . . . . . . . . . . : "); kputs(ip); kputc('\n');
    ip_format_ipv4(status.local_gw, ip, sizeof(ip));
    kputs("   Default Gateway . . . . . . . . : "); kputs(ip); kputc('\n');
    if (nic_get_mac(mac) == 0 && ipv6_link_local_from_mac(mac, link_local) == 0) {
        ipv6_format(link_local, ip, sizeof(ip));
        kputs("   Link-local IPv6 Address . . . . : "); kputs(ip); kputc('\n');
    }
    if (status.local_ipv6[0] != 0u) {
        ipv6_format(status.local_ipv6, ip, sizeof(ip));
        kputs("   IPv6 Stack Address . . . . . . . : "); kputs(ip); kputc('\n');
    }
    kputs("   IPv6 RX Packets . . . . . . . . : "); print_u32(status.ipv6_rx); kputc('\n');
    kputs("   NDP Neighbor Entries . . . . . . : "); print_u32(status.ndp_entries); kputc('\n');
    kputs("   DHCP/ARP State . . . . . . . . . : ");
    kputs(status.local_ip != 0u ? "Configured\n" : "Not configured\n");
    kputs("   ARP Entries . . . . . . . . . . : "); print_u32(status.arp_entries); kputc('\n');
}

static void cmd_ping6(const char* args) {
    char text[96];
    char formatted[64];
    u8 remote[16];
    u32 elapsed = 0u;
    args = read_token(skip_space(args), text, sizeof(text));
    if (!text[0] || (ipv6_parse(text, remote) != 0 && dns_resolve_ipv6(text, remote, 200u) != 0)) {
        kputs("Usage: ping6 <ipv6-address>\n");
        return;
    }
    ipv6_format(remote, formatted, sizeof(formatted));
    kputs("PING6 "); kputs(formatted); kputs(" 32 bytes of data.\n");
    if (knetstack_ipv6_ping(remote, 1u, 200u, &elapsed) != 0) {
        kputs("Request timed out.\n");
        return;
    }
    kputs("Reply from "); kputs(formatted); kputs(": bytes=32 time=");
    print_u32((elapsed * 1000u) / (ktimer_frequency() ? ktimer_frequency() : 1u));
    kputs("ms\n");
}

static void cmd_ping(const char* args) {
    char host[96], opt[16];
    char ip_text[32];
    u32 ip = 0u, count = 4u, sent = 0u, received = 0u;
    u32 min = 0xFFFFFFFFu, max = 0u, sum = 0u;
    const char* rest = read_token(skip_space(args), host, sizeof(host));
    rest = read_token(rest, opt, sizeof(opt));
    if (!host[0]) {
        kputs("Usage: ping <host|ip> [-c count]\n");
        return;
    }
    if (opt[0] && str_cmp(opt, "-c") == 0) {
        char nbuf[16];
        (void)read_token(rest, nbuf, sizeof(nbuf));
        if (parse_u32(nbuf, &count) != 0 || count == 0u || count > 32u) count = 4u;
    }
    if (shell_resolve_host(host, &ip) != 0) {
        kputs("ping: name resolution failed\n");
        return;
    }
    ip_format_ipv4(ip, ip_text, sizeof(ip_text));
    kputs("PING "); kputs(host); kputs(" ("); kputs(ip_text); kputs(") 32 bytes of data.\n");
    for (u32 i = 0u; i < count; i++) {
        u32 elapsed = 0u;
        u8 ttl = 0u;
        sent++;
        if (knetstack_ping(ip, (u16)(i + 1u), 200u, &elapsed, &ttl) == 0) {
            u32 hz = ktimer_frequency();
            u32 ms = hz ? (elapsed * 1000u) / hz : elapsed;
            received++;
            if (ms < min) min = ms;
            if (ms > max) max = ms;
            sum += ms;
            kputs("Reply from "); kputs(ip_text); kputs(": bytes=32 time="); print_u32(ms);
            kputs("ms TTL="); print_u32(ttl); kputc('\n');
        } else {
            kputs("Request timed out.\n");
        }
    }
    kputs("\nPing statistics for "); kputs(ip_text); kputs(":\n");
    kputs("Packets: Sent = "); print_u32(sent); kputs(", Received = "); print_u32(received);
    kputs(", Lost = "); print_u32(sent - received); kputs(" (");
    print_u32(sent ? ((sent - received) * 100u) / sent : 0u); kputs("% loss)\n");
    if (received) {
        kputs("Approximate round trip times in milli-seconds:\nMinimum = "); print_u32(min);
        kputs("ms, Maximum = "); print_u32(max); kputs("ms, Average = "); print_u32(sum / received); kputs("ms\n");
    }
}

static void cmd_nat(const char* args) {
    char sub[16], t0[32], t1[32], t2[32], t3[32];
    u32 idx = 0u, port = 0u, int_port = 0u, ip = 0u;
    int ok = 0;
    nat_status_t st;

    args = read_token(skip_space(args), sub, sizeof(sub));

    if (!sub[0] || str_cmp(sub, "status") == 0 || str_cmp(sub, "show") == 0) {
        knat_status(&st);
        kputs("NAT status:\n");
        kputs("  IP forward: "); kputs(st.ip_forward ? "enabled\n" : "disabled\n");
        kputs("  Conntrack entries: "); print_u32(st.conntrack_count); kputc('\n');
        kputs("  Rules: "); print_u32(st.rule_count); kputc('\n');
        kputs("  SNAT packets: "); print_u32(st.snat_packets); kputc('\n');
        kputs("  DNAT packets: "); print_u32(st.dnat_packets); kputc('\n');

        /* Show conntrack table */
        u32 ct_cnt = knat_conntrack_count();
        if (ct_cnt > 0u) {
            char ipb[32], ipb2[32];
            kputs("\nConnection tracking:\n");
            kputs("  Proto  Src IP:Port            Dst IP:Port            State\n");
            for (u32 i = 0u; i < ct_cnt; i++) {
                conntrack_entry_t ct;
                if (knat_conntrack_get(i, &ct) != 0) break;
                ip_format_ipv4(ct.key.src_ip, ipb, sizeof(ipb));
                ip_format_ipv4(ct.key.dst_ip, ipb2, sizeof(ipb2));
                kputs("  ");
                kputs(ct.key.proto == IP_PROTO_TCP ? "TCP " : (ct.key.proto == IP_PROTO_UDP ? "UDP " : "ICMP"));
                kputs("  "); kputs(ipb); kputs(":"); print_u32(ct.key.src_port);
                kputs(" -> "); kputs(ipb2); kputs(":"); print_u32(ct.key.dst_port);
                kputs(" "); kputs(ct.state == CT_NEW ? "NEW" : (ct.state == CT_ESTABLISHED ? "ESTAB" : "CLOSE"));
                kputc('\n');
            }
        }

        /* Show rules */
        u32 rule_cnt = knat_rule_count();
        if (rule_cnt > 0u) {
            char ipb[32];
            kputs("\nNAT rules:\n");
            for (u32 i = 0u; i < rule_cnt; i++) {
                nat_rule_t r;
                if (knat_rule_get(i, &r) != 0) break;
                if (r.type == NAT_TYPE_DNAT) {
                    ip_format_ipv4(r.internal_ip, ipb, sizeof(ipb));
                    kputs("  DNAT: ");
                    kputs(r.proto == IP_PROTO_TCP ? "TCP " : (r.proto == IP_PROTO_UDP ? "UDP " : "ANY "));
                    kputs(":"); print_u32(r.external_port);
                    kputs(" -> "); kputs(ipb); kputs(":"); print_u32(r.internal_port ? r.internal_port : r.external_port);
                    kputc('\n');
                } else if (r.type == NAT_TYPE_SNAT) {
                    char nb[32], mb[32];
                    ip_format_ipv4(r.internal_net, nb, sizeof(nb));
                    ip_format_ipv4(r.internal_mask, mb, sizeof(mb));
                    kputs("  SNAT: "); kputs(nb); kputs("/"); kputs(mb);
                    kputs(" -> external IP");
                    kputc('\n');
                }
            }
        }
        return;
    }

    if (str_cmp(sub, "forward") == 0) {
        args = read_token(skip_space(args), t0, sizeof(t0));
        if (str_cmp(t0, "on") == 0 || str_cmp(t0, "enable") == 0) {
            knat_set_ip_forward(1u);
            kputs("IP forwarding enabled\n");
        } else if (str_cmp(t0, "off") == 0 || str_cmp(t0, "disable") == 0) {
            knat_set_ip_forward(0u);
            kputs("IP forwarding disabled\n");
        } else {
            kputs("Usage: nat forward on|off\n");
        }
        return;
    }

    if (str_cmp(sub, "dnat") == 0) {
        /* nat dnat <proto> <ext_port> <internal_ip> [internal_port] */
        args = read_token(skip_space(args), t0, sizeof(t0));  /* proto */
        args = read_token(skip_space(args), t1, sizeof(t1));  /* ext_port */
        args = read_token(skip_space(args), t2, sizeof(t2));  /* int_ip */
        (void)read_token(args, t3, sizeof(t3));               /* int_port */
        u8 proto = (str_cmp(t0, "tcp") == 0) ? IP_PROTO_TCP :
                   (str_cmp(t0, "udp") == 0) ? IP_PROTO_UDP : 0u;
        if (proto == 0u || !t1[0] || !t2[0] ||
            parse_u32(t1, &port) != 0 || port == 0u || port > 65535u) {
            kputs("Usage: nat dnat <tcp|udp> <external_port> <internal_ip> [internal_port]\n");
            return;
        }
        ip = ip_parse_ipv4(t2, &ok);
        if (!ok) { kputs("nat: invalid internal IP\n"); return; }
        if (t3[0]) {
            if (parse_u32(t3, &int_port) != 0 || int_port == 0u || int_port > 65535u) {
                kputs("nat: invalid internal port\n"); return;
            }
        }
        if (knat_rule_add(NAT_TYPE_DNAT, proto, (u16)port, ip, (u16)int_port, 0u, 0u) != 0) {
            kputs("nat: rule table full\n");
        } else {
            kputs("DNAT rule added\n");
        }
        return;
    }

    if (str_cmp(sub, "snat") == 0) {
        /* nat snat <network> <mask> */
        u32 net = 0u, mask = 0u;
        args = read_token(skip_space(args), t0, sizeof(t0));
        args = read_token(skip_space(args), t1, sizeof(t1));
        if (!t0[0] || !t1[0]) {
            kputs("Usage: nat snat <network> <mask>\n");
            return;
        }
        net = ip_parse_ipv4(t0, &ok); if (!ok) { kputs("nat: invalid network\n"); return; }
        mask = ip_parse_ipv4(t1, &ok); if (!ok) { kputs("nat: invalid mask\n"); return; }
        if (knat_rule_add(NAT_TYPE_SNAT, 0u, 0u, 0u, 0u, net, mask) != 0) {
            kputs("nat: rule table full\n");
        } else {
            kputs("SNAT rule added\n");
        }
        return;
    }

    if (str_cmp(sub, "del") == 0 || str_cmp(sub, "delete") == 0) {
        args = read_token(skip_space(args), t0, sizeof(t0));
        if (!t0[0] || parse_u32(t0, &idx) != 0) {
            kputs("Usage: nat delete <rule_index>\n");
            return;
        }
        if (knat_rule_del(idx) != 0) {
            kputs("nat: rule not found\n");
        } else {
            kputs("NAT rule deleted\n");
        }
        return;
    }

    if (str_cmp(sub, "clear") == 0 || str_cmp(sub, "flush") == 0) {
        /* Delete all rules */
        u32 cnt = knat_rule_count();
        for (u32 i = 0u; i < cnt; i++) knat_rule_del(0u);  /* Always delete index 0 */
        kputs("All NAT rules cleared\n");
        return;
    }

    kputs("Usage: nat [status|show]|forward on|off|dnat <tcp|udp> <ext_port> <int_ip> [int_port]|snat <net> <mask>|del <idx>|clear\n");
}

static void cmd_dns_server(const char* args) {
    char sub[16], t0[32], t1[32], t2[32], t3[32], t4[32], t5[32];
    dns_server_status_t st;
    args = read_token(skip_space(args), sub, sizeof(sub));

    if (!sub[0] || str_cmp(sub, "status") == 0 || str_cmp(sub, "show") == 0) {
        kdns_server_status(&st);
        kputs("DNS Server:\n");
        kputs("  Active: "); kputs(st.active ? "yes\n" : "no\n");
        if (st.active) {
            kputs("  Queries total/cached/forwarded/failed/throttled: ");
            print_u32(st.queries_total); kputs("/");
            print_u32(st.queries_cached); kputs("/");
            print_u32(st.queries_forwarded); kputs("/");
            print_u32(st.queries_failed); kputs("/");
            print_u32(st.queries_throttled); kputc('\n');
        }
        return;
    }

    if (str_cmp(sub, "start") == 0) {
        if (kdns_server_start() == 0) kputs("DNS server started on UDP:53\n");
        else kputs("DNS server: failed to start\n");
        return;
    }

    if (str_cmp(sub, "stop") == 0) {
        kdns_server_stop();
        kputs("DNS server stopped\n");
        return;
    }

    kputs("Usage: dns-server start|stop|status\n");
}

static void cmd_dhcp_server(const char* args) {
    char sub[16], t0[32], t1[32], t2[32], t3[32], t4[32], t5[32];
    u32 pool_start = 0u, pool_end = 0u, mask = 0u, gw = 0u, dns = 0u, lease = 0u;
    int ok = 0;
    dhcp_server_status_t st;
    args = read_token(skip_space(args), sub, sizeof(sub));

    if (!sub[0] || str_cmp(sub, "status") == 0 || str_cmp(sub, "show") == 0) {
        kdhcp_server_status(&st);
        kputs("DHCP Server:\n");
        kputs("  Active: "); kputs(st.active ? "yes\n" : "no\n");
        if (st.pool_start) {
            char ipb[32];
            ip_format_ipv4(st.pool_start, ipb, sizeof(ipb));
            kputs("  Pool: "); kputs(ipb);
            ip_format_ipv4(st.pool_end, ipb, sizeof(ipb));
            kputs(" - "); kputs(ipb); kputc('\n');
            ip_format_ipv4(st.subnet_mask, ipb, sizeof(ipb));
            kputs("  Mask: "); kputs(ipb); kputc('\n');
            ip_format_ipv4(st.gateway, ipb, sizeof(ipb));
            kputs("  Gateway: "); kputs(ipb); kputc('\n');
            ip_format_ipv4(st.dns_server, ipb, sizeof(ipb));
            kputs("  DNS: "); kputs(ipb); kputc('\n');
        }
        kputs("  Offers/ACKs/Declines: ");
        print_u32(st.offers_sent); kputs("/");
        print_u32(st.acks_sent); kputs("/");
        print_u32(st.declines_received); kputc('\n');
        kputs("  Active Leases: "); print_u32(st.leases_active); kputc('\n');

        /* Show leases */
        u32 lcnt = kdhcp_server_lease_count();
        if (lcnt > 0u) {
            char ipb[32];
            kputs("  Leases:\n");
            for (u32 i = 0u; i < lcnt; i++) {
                dhcp_lease_t l;
                if (kdhcp_server_lease_get(i, &l) != 0) break;
                ip_format_ipv4(l.offered_ip, ipb, sizeof(ipb));
                kputs("    "); kputs(ipb); kputs("  MAC: ");
                for (u32 m = 0u; m < 6u; m++) {
                    u8 v = l.mac[m];
                    kputc((v >> 4u) < 10u ? ('0' + (v >> 4u)) : ('A' + (v >> 4u) - 10));
                    kputc((v & 0xFu) < 10u ? ('0' + (v & 0xFu)) : ('A' + (v & 0xFu) - 10));
                    if (m < 5u) kputc(':');
                }
                kputc('\n');
            }
        }
        return;
    }

    if (str_cmp(sub, "configure") == 0) {
        /* dhcp-server configure <pool_start> <pool_end> <mask> <gateway> [dns] [lease_ticks] */
        args = read_token(skip_space(args), t0, sizeof(t0));
        args = read_token(skip_space(args), t1, sizeof(t1));
        args = read_token(skip_space(args), t2, sizeof(t2));
        args = read_token(skip_space(args), t3, sizeof(t3));
        (void)read_token(args, t4, sizeof(t4));
        if (!t0[0] || !t1[0] || !t2[0] || !t3[0]) {
            kputs("Usage: dhcp-server configure <pool_start> <pool_end> <mask> <gateway> [dns] [lease_ticks]\n");
            return;
        }
        pool_start = ip_parse_ipv4(t0, &ok); if (!ok) { kputs("dhcp: invalid pool_start\n"); return; }
        pool_end   = ip_parse_ipv4(t1, &ok); if (!ok) { kputs("dhcp: invalid pool_end\n"); return; }
        mask       = ip_parse_ipv4(t2, &ok); if (!ok) { kputs("dhcp: invalid mask\n"); return; }
        gw         = ip_parse_ipv4(t3, &ok); if (!ok) { kputs("dhcp: invalid gateway\n"); return; }
        if (t4[0]) {
            dns = ip_parse_ipv4(t4, &ok);
            if (!ok) { kputs("dhcp: invalid dns\n"); return; }
        }
        if (lease == 0u) lease = 6000u;

        if (kdhcp_server_configure(pool_start, pool_end, mask, gw, dns, lease) == 0)
            kputs("DHCP server configured\n");
        else
            kputs("dhcp: invalid configuration\n");
        return;
    }

    if (str_cmp(sub, "start") == 0) {
        if (kdhcp_server_start() == 0) kputs("DHCP server started on UDP:67\n");
        else kputs("DHCP server: failed to start (configure first)\n");
        return;
    }

    if (str_cmp(sub, "stop") == 0) {
        kdhcp_server_stop();
        kputs("DHCP server stopped\n");
        return;
    }

    kputs("Usage: dhcp-server configure <pool_start> <pool_end> <mask> <gateway> [dns] [lease_ticks]|start|stop|status\n");
}

static void cmd_arp(const char* args) {
    netstack_status_t st;
    char ip_text[32];
    u8 broadcast_mac[6] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu };
    u8 multicast_mac[6];
    (void)args;
    knetstack_status(&st);
    ip_format_ipv4(st.local_ip, ip_text, sizeof(ip_text));
    kputs("Interface: "); kputs(ip_text); kputs("\n");
    kputs("  Internet Address      Physical Address      Type\n");
    for (u32 i = 0u; i < knetstack_arp_count(); i++) {
        net_arp_entry_t e;
        if (knetstack_arp_get(i, &e) != 0) continue;
        ip_format_ipv4(e.ip, ip_text, sizeof(ip_text));
        kputs("  "); kputs(ip_text); kputs("      "); shell_print_mac(e.mac, '-');
        kputs("      dynamic\n");
    }
    if (st.local_mask != 0u) {
        u32 subnet_broadcast = (st.local_ip & st.local_mask) | ~st.local_mask;
        ip_format_ipv4(subnet_broadcast, ip_text, sizeof(ip_text));
        kputs("  "); kputs(ip_text); kputs("      "); shell_print_mac(broadcast_mac, '-');
        kputs("      static\n");
    }
    ip_format_ipv4(0xFFFFFFFFu, ip_text, sizeof(ip_text));
    kputs("  "); kputs(ip_text); kputs("      "); shell_print_mac(broadcast_mac, '-');
    kputs("      static\n");
    /* Well-known IPv4 multicast mappings are predictable Ethernet entries. */
    multicast_mac[0] = 0x01u; multicast_mac[1] = 0x00u; multicast_mac[2] = 0x5Eu;
    multicast_mac[3] = 0x00u; multicast_mac[4] = 0x00u; multicast_mac[5] = 0x16u;
    ip_format_ipv4(0xE0000016u, ip_text, sizeof(ip_text));
    kputs("  "); kputs(ip_text); kputs("      "); shell_print_mac(multicast_mac, '-'); kputs("      static\n");
    multicast_mac[5] = 0xFBu;
    ip_format_ipv4(0xE00000FBu, ip_text, sizeof(ip_text));
    kputs("  "); kputs(ip_text); kputs("      "); shell_print_mac(multicast_mac, '-'); kputs("      static\n");
    multicast_mac[5] = 0xFCu;
    ip_format_ipv4(0xE00000FCu, ip_text, sizeof(ip_text));
    kputs("  "); kputs(ip_text); kputs("      "); shell_print_mac(multicast_mac, '-'); kputs("      static\n");
    multicast_mac[3] = 0x7Fu; multicast_mac[4] = 0xFFu; multicast_mac[5] = 0xFAu;
    ip_format_ipv4(0xEFFFFFFAu, ip_text, sizeof(ip_text));
    kputs("  "); kputs(ip_text); kputs("      "); shell_print_mac(multicast_mac, '-'); kputs("      static\n");
}

static void cmd_curl(const char* args) {
    char url[192], host[96], path[128], token[24], request[1024];
    u32 ip = 0u, port = 80u, local_port = 49152u + (kticks() & 0x0FFFu);
    u32 host_len = 0u, path_len = 0u, i = 0u;
    u8 ip6[16];
    int use_ipv6 = 0;
    int use_tls = 0;
    int sock;
    const char* p;
    (void)read_token(skip_space(args), url, sizeof(url));
    if (!url[0]) {
        kputs("Usage: curl http://<host>[:port]/<path>\n");
        return;
    }
    if (starts_with(url, "https://")) { use_tls = 1; port = 443u; }
    /* A bare domain is a normal HTTP convenience form: curl example.com. */
    p = starts_with(url, "http://") ? url + 7u : (starts_with(url, "https://") ? url + 8u : url);
    if (*p == '[') {
        p++;
        while (p[host_len] && p[host_len] != ']') host_len++;
        use_ipv6 = 1;
    } else {
        while (p[host_len] && p[host_len] != '/' && p[host_len] != ':') host_len++;
    }
    if (host_len == 0u || host_len >= sizeof(host)) { kputs("curl: invalid host\n"); return; }
    for (i = 0u; i < host_len; i++) host[i] = p[i];
    host[host_len] = '\0';
    p += host_len;
    if (use_ipv6 && *p == ']') p++;
    if (*p == ':') {
        const char* port_start;
        u32 port_len = 0u;
        p++;
        port_start = p;
        while (p[port_len] >= '0' && p[port_len] <= '9') port_len++;
        if (port_len == 0u || port_len >= sizeof(token)) { kputs("curl: invalid port\n"); return; }
        for (i = 0u; i < port_len; i++) token[i] = port_start[i];
        token[port_len] = '\0';
        if (parse_u32(token, &port) != 0 || port == 0u || port > 65535u) { kputs("curl: invalid port\n"); return; }
        p += port_len;
    }
    if (*p == '/') {
        p++;
        path[0] = '/';
        path_len = 1u;
        while (p[path_len - 1u] && path_len + 1u < sizeof(path)) { path[path_len] = p[path_len - 1u]; path_len++; }
        path[path_len] = '\0';
    } else {
        str_cpy(path, "/");
    }
    if (use_ipv6) {
        if (shell_resolve_host6(host, ip6) != 0) { kputs("curl: IPv6 name resolution failed\n"); return; }
        sock = knetstack_socket_open6(IP_PROTO_TCP, (u16)local_port, ip6, (u16)port);
    } else {
        if (shell_resolve_host(host, &ip) != 0) {
            if (shell_resolve_host6(host, ip6) != 0) { kputs("curl: name resolution failed\n"); return; }
            use_ipv6 = 1;
            sock = knetstack_socket_open6(IP_PROTO_TCP, (u16)local_port, ip6, (u16)port);
        } else {
            sock = knetstack_socket_open(IP_PROTO_TCP, (u16)local_port, ip, (u16)port);
        }
    }
    if (sock < 0 || knetstack_socket_send((u32)sock, 0, 0u) != 0) {
        if (sock >= 0) (void)knetstack_socket_close((u32)sock);
        kputs("curl: connect failed\n");
        return;
    }
    u32 deadline = kticks() + 300u;
    u8 state = 0u;
    while ((i32)(kticks() - deadline) < 0) {
        knetstack_poll(16u);
        (void)knetstack_socket_state((u32)sock, &state);
        if (state == NET_SOCKET_ESTABLISHED) break;
    }
    if (state != NET_SOCKET_ESTABLISHED) {
        (void)knetstack_socket_close((u32)sock);
        kputs("curl: TCP handshake timeout\n");
        return;
    }
    if (use_tls) {
        int tls_result = tls_http_get((u32)sock, host, path, shell_tls_print_body, 0);
        (void)knetstack_socket_close((u32)sock);
        if (tls_result != 0) kputs("curl: TLS handshake or certificate validation failed\n");
        else kputc('\n');
        return;
    }
    str_cpy(request, "GET "); safe_append(request, sizeof(request), path); safe_append(request, sizeof(request), " HTTP/1.0\r\nHost: ");
    safe_append(request, sizeof(request), host); safe_append(request, sizeof(request), "\r\nConnection: close\r\n\r\n");
    if (knetstack_socket_send((u32)sock, (const u8*)request, (u16)str_len(request)) != 0) {
        (void)knetstack_socket_close((u32)sock);
        kputs("curl: send failed\n");
        return;
    }
    deadline = kticks() + 500u;
    while ((i32)(kticks() - deadline) < 0) {
        u8 data[1024];
        knetstack_poll(16u);
        int n = knetstack_socket_recv((u32)sock, data, sizeof(data), 0, 0);
        if (n > 0) {
            for (int j = 0; j < n; j++) kputc((char)data[j]);
            deadline = kticks() + 500u;
            continue;
        }
        (void)knetstack_socket_state((u32)sock, &state);
        if (state == NET_SOCKET_CLOSING || state == 0u) break;
    }
    (void)knetstack_socket_close((u32)sock);
}

static void cmd_wget(const char* args) {
    char url[192], host[96], path[128], number[24], option[24], output_name[VFS_NAME_MAX], output_path[128], request[1024], header[512];
    u32 ip = 0u, port = 80u, local_port = 52000u + (kticks() & 0x07FFu);
    u32 host_len = 0u, i = 0u, header_len = 0u, downloaded = 0u, resume_at = 0u, deadline;
    u8 header_match = 0u, state = 0u, body_started = 0u, created = 0u;
    u8 want_resume = 0u;
    int sock;
    const char* p;
    vfs_node_info_t info;

    memset(url, 0, sizeof(url)); memset(option, 0, sizeof(option)); memset(output_name, 0, sizeof(output_name));
    args = read_token(skip_space(args), url, sizeof(url));
    if (!url[0]) { kputs("Usage: wget [http://]<host>[:port]/<path> [-O file]\n"); return; }
    if (starts_with(url, "https://")) { kputs("wget: HTTPS requires the TLS transport module\n"); return; }
    p = starts_with(url, "http://") ? url + 7u : url;
    while (p[host_len] && p[host_len] != '/' && p[host_len] != ':') host_len++;
    if (host_len == 0u || host_len >= sizeof(host)) { kputs("wget: invalid host\n"); return; }
    for (i = 0u; i < host_len; i++) host[i] = p[i];
    host[host_len] = '\0';
    p += host_len;
    if (*p == ':') {
        const char* port_start;
        u32 port_len = 0u;
        p++; port_start = p;
        while (p[port_len] >= '0' && p[port_len] <= '9') port_len++;
        if (port_len == 0u || port_len >= sizeof(number)) { kputs("wget: invalid port\n"); return; }
        for (i = 0u; i < port_len; i++) number[i] = port_start[i];
        number[port_len] = '\0';
        if (parse_u32(number, &port) != 0 || port == 0u || port > 65535u) { kputs("wget: invalid port\n"); return; }
        p += port_len;
    }
    if (*p == '/') str_ncpy(path, p, sizeof(path) - 1u); else str_cpy(path, "/");
    path[sizeof(path) - 1u] = '\0';
    while (*(args = skip_space(args))) {
        args = read_token(args, option, sizeof(option));
        if (str_cmp(option, "-c") == 0 || str_cmp(option, "--continue") == 0) {
            want_resume = 1u;
        } else if (str_cmp(option, "-O") == 0) {
            args = read_token(args, output_name, sizeof(output_name));
            if (!output_name[0]) { kputs("wget: -O requires a file name\n"); return; }
        } else {
            kputs("wget: unknown option (use -c or -O file)\n"); return;
        }
    }
    if (!output_name[0]) {
        const char* leaf = path;
        for (i = 0u; path[i]; i++) if (path[i] == '/') leaf = path + i + 1u;
        if (leaf[0]) str_ncpy(output_name, leaf, sizeof(output_name) - 1u);
        else str_cpy(output_name, "index.html");
    }
    output_name[sizeof(output_name) - 1u] = '\0';
    kvfs_make_path(output_name, output_path, sizeof(output_path));
    if (kvfs_info(output_path, &info) != 0) {
        if (kvfs_create_file(output_path) != 0) { kputs("wget: cannot create output file\n"); return; }
        created = 1u;
    } else if (info.type != VFS_NODE_FILE) { kputs("wget: output path is not a file\n"); return; }
    if (want_resume) { resume_at = info.size; downloaded = resume_at; }
    kvfs_begin_batch();
    if (!want_resume && kvfs_write_file(output_path, "", 0u) != 0) goto download_failed;
    if (shell_resolve_host(host, &ip) != 0) { kputs("wget: name resolution failed\n"); goto download_failed; }
    sock = knetstack_socket_open(IP_PROTO_TCP, (u16)local_port, ip, (u16)port);
    if (sock < 0 || knetstack_socket_send((u32)sock, 0, 0u) != 0) { kputs("wget: connect failed\n"); goto download_failed; }
    deadline = kticks() + 500u;
    while ((i32)(kticks() - deadline) < 0) {
        knetstack_poll(16u);
        (void)knetstack_socket_state((u32)sock, &state);
        if (state == NET_SOCKET_ESTABLISHED) break;
    }
    if (state != NET_SOCKET_ESTABLISHED) { (void)knetstack_socket_close((u32)sock); kputs("wget: TCP handshake timeout\n"); goto download_failed; }
    str_cpy(request, "GET "); safe_append(request, sizeof(request), path); safe_append(request, sizeof(request), " HTTP/1.1\r\nHost: ");
    safe_append(request, sizeof(request), host);
    safe_append(request, sizeof(request), "\r\nUser-Agent: Mozilla/5.0 (X11; LastOS x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Super-NetSurf/3.11 QuickJS/2021 Safari/537.36\r\n");
    safe_append(request, sizeof(request), "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n");
    safe_append(request, sizeof(request), "Accept-Language: fa-IR,fa;q=0.9,en-US;q=0.8,en;q=0.7\r\n");
    if (resume_at) {
        safe_append(request, sizeof(request), "Range: bytes=");
        { char digits[12]; u32 v = resume_at, d = 0u; do { digits[d++] = (char)('0' + (v % 10u)); v /= 10u; } while (v && d < sizeof(digits)); while (d) { u32 at = str_len(request); request[at] = digits[--d]; request[at + 1u] = '\0'; } }
        safe_append(request, sizeof(request), "-\r\n");
    }
    safe_append(request, sizeof(request), "Connection: close\r\n\r\n");
    if (knetstack_socket_send((u32)sock, (const u8*)request, (u16)str_len(request)) != 0) { (void)knetstack_socket_close((u32)sock); kputs("wget: send failed\n"); goto download_failed; }
    deadline = kticks() + 1000u;
    while ((i32)(kticks() - deadline) < 0) {
        u8 data[1024];
        int n;
        knetstack_poll(16u);
        n = knetstack_socket_recv((u32)sock, data, sizeof(data), 0, 0);
        if (n > 0) {
            u32 body_offset = 0u;
            deadline = kticks() + 1000u;
            for (i = 0u; i < (u32)n && !body_started; i++) {
                if (header_len + 1u >= sizeof(header)) { (void)knetstack_socket_close((u32)sock); kputs("wget: HTTP headers too large\n"); goto download_failed; }
                header[header_len++] = (char)data[i];
                if ((header_match == 0u && data[i] == '\r') || (header_match == 1u && data[i] == '\n') ||
                    (header_match == 2u && data[i] == '\r') || (header_match == 3u && data[i] == '\n')) header_match++;
                else header_match = data[i] == '\r' ? 1u : 0u;
                if (header_match == 4u) {
                    header[header_len] = '\0';
                    if (header_len < 12u || header[9] != '2' || (resume_at && header[9] != '2')) { (void)knetstack_socket_close((u32)sock); kputs("wget: HTTP request failed\n"); goto download_failed; }
                    if (resume_at && !(header[9] == '2' && header[10] == '0' && header[11] == '6')) {
                        /* A server that ignored Range must not append a second copy. */
                        if (kvfs_write_file(output_path, "", 0u) != 0) { (void)knetstack_socket_close((u32)sock); goto download_failed; }
                        resume_at = 0u;
                        downloaded = 0u;
                    }
                    body_started = 1u;
                    body_offset = i + 1u;
                }
            }
            if (body_started && body_offset < (u32)n) {
                u32 body_len = (u32)n - body_offset;
                if (kvfs_append_file(output_path, (const char*)data + body_offset, body_len) != 0) { (void)knetstack_socket_close((u32)sock); kputs("wget: disk full or file exceeds the current VFS capacity\n"); goto download_failed; }
                downloaded += body_len;
            }
            continue;
        }
        (void)knetstack_socket_state((u32)sock, &state);
        if (state == NET_SOCKET_CLOSING || state == 0u) break;
    }
    (void)knetstack_socket_close((u32)sock);
    if (!body_started) { (void)kvfs_end_batch(); kputs("wget: transfer did not complete\n"); return; }
    {
        int flush_result = kvfs_end_batch();
        kputs("saved "); kputs(output_path); kputs(" ("); print_u32(downloaded); kputs(" bytes total)");
        if (flush_result != 0) kputs(" [live VFS: no persistent disk]");
        kputc('\n');
    }
    return;

download_failed:
    (void)kvfs_end_batch();
    if (created) (void)kvfs_delete(output_path);
}

static void cmd_dig(const char* args) {
    char host[96];
    char address[32], address6[64];
    u32 ip = 0u;
    u8 ip6[16];
    int have4, have6, cached4, cached6;
    u8 cached4_addr[4];
    (void)read_token(skip_space(args), host, sizeof(host));
    if (!host[0]) { kputs("Usage: dig <host>\n"); return; }
    cached4 = dns_cache_lookup(host, 4u, cached4_addr) == 0;
    if (cached4) ip = ((u32)cached4_addr[0] << 24) | ((u32)cached4_addr[1] << 16) | ((u32)cached4_addr[2] << 8) | cached4_addr[3];
    have4 = shell_resolve_host(host, &ip) == 0;
    cached6 = dns_cache_lookup(host, 6u, ip6) == 0;
    have6 = shell_resolve_host6(host, ip6) == 0;
    kputs("; <<>> LastOS dig <<>> "); kputs(host); kputc('\n');
    if (!have4 && !have6) { kputs(";; status: SERVFAIL\n"); kputs("dig: name resolution failed\n"); return; }
    kputs(";; status: NOERROR\n\n;; ANSWER SECTION:\n");
    if (have4) { ip_format_ipv4(ip, address, sizeof(address)); kputs(host); kputs(".\tIN\tA\t"); kputs(address); kputs(cached4 ? "\t(cache)\n" : "\n"); }
    if (have6) { ipv6_format(ip6, address6, sizeof(address6)); kputs(host); kputs(".\tIN\tAAAA\t"); kputs(address6); kputs(cached6 ? "\t(cache)\n" : "\n"); }
}

static void cmd_whois(const char* args) {
    char query[96];
    char request[128];
    u32 ip = 0u;
    u32 local_port = 50000u + (kticks() & 0x07FFu);
    int sock;
    u8 state = 0u;
    u32 deadline;

    (void)read_token(skip_space(args), query, sizeof(query));
    if (!query[0]) { kputs("Usage: whois <domain|ip>\n"); return; }
    if (shell_resolve_host(query, &ip) != 0) {
        kputs("whois: name resolution failed\n");
        return;
    }
    sock = knetstack_socket_open(IP_PROTO_TCP, (u16)local_port, ip, 43u);
    if (sock < 0 || knetstack_socket_send((u32)sock, 0, 0u) != 0) {
        if (sock >= 0) (void)knetstack_socket_close((u32)sock);
        kputs("whois: connect failed\n");
        return;
    }
    deadline = kticks() + 300u;
    while ((i32)(kticks() - deadline) < 0) {
        knetstack_poll(16u);
        (void)knetstack_socket_state((u32)sock, &state);
        if (state == NET_SOCKET_ESTABLISHED) break;
    }
    if (state != NET_SOCKET_ESTABLISHED) {
        (void)knetstack_socket_close((u32)sock);
        kputs("whois: TCP handshake timeout\n");
        return;
    }
    str_cpy(request, query); safe_append(request, sizeof(request), "\r\n");
    if (knetstack_socket_send((u32)sock, (const u8*)request, (u16)str_len(request)) != 0) {
        (void)knetstack_socket_close((u32)sock);
        kputs("whois: send failed\n");
        return;
    }
    deadline = kticks() + 500u;
    while ((i32)(kticks() - deadline) < 0) {
        u8 data[1024];
        int n;
        knetstack_poll(16u);
        n = knetstack_socket_recv((u32)sock, data, sizeof(data), 0, 0);
        if (n > 0) {
            for (int i = 0; i < n; i++) kputc((char)data[i]);
            continue;
        }
        (void)knetstack_socket_state((u32)sock, &state);
        if (state == NET_SOCKET_CLOSING || state == 0u) break;
    }
    (void)knetstack_socket_close((u32)sock);
}

static void cmd_httpd(const char* args) {
    char action[16], value0[96], value1[32], value2[16];
    u32 port = 8080u, ip;
    int ok = 0;
    const char* rest = read_token(skip_space(args), action, sizeof(action));

    if (str_cmp(action, "status") == 0) {
        u16 listen_port = 0u;
        u32 requests = 0u, errors = 0u;
        u8 proxy = 0u;
        char root[64];
        memset(root, 0, sizeof(root));
        httpd_status(&listen_port, &requests, &errors, &proxy, root, sizeof(root));
        kputs("HTTP/HTTPS server: "); kputs((g_user_httpd_pid || httpd_active() || httpd_tls_active()) ? "active\n" : "stopped\n");
        if (g_user_httpd_pid) { kputs("user-mode httpd pid: "); print_u32(g_user_httpd_pid); kputc('\n'); }
        kputs("HTTPS listener: "); kputs(httpd_tls_active() ? "active\n" : "stopped\n");
        kputs("port/root: "); print_u32(listen_port); kputc('/'); kputs(root[0] ? root : "-");
        kputs("\nrequests/errors/proxy: "); print_u32(requests); kputc('/'); print_u32(errors); kputc('/'); print_u32(proxy); kputc('\n');
        return;
    }
    if (str_cmp(action, "start") == 0) {
        rest = read_token(rest, value1, sizeof(value1));
        rest = read_token(rest, value0, sizeof(value0));
        if (value1[0] && (parse_u32(value1, &port) != 0 || port == 0u || port > 65535u)) {
            kputs("httpd: invalid port\n"); return;
        }
        if (!value0[0]) str_cpy(value0, "/www");
        if (port != 8080u || g_user_httpd_pid) {
            kputs("httpd: user service uses port 8080 or is already running\n");
            return;
        }
        g_last_spawn_pid = 0u;
        cmd_spawn("/bin/httpd.elf");
        g_user_httpd_pid = g_last_spawn_pid;
        if (!g_user_httpd_pid) kputs("httpd: failed to start user service\n");
        else { kputs("User-mode HTTP server listening on port 8080 pid="); print_u32(g_user_httpd_pid); kputc('\n'); }
        return;
    }
    if (str_cmp(action, "start-tls") == 0) {
        rest = read_token(rest, value1, sizeof(value1));
        rest = read_token(rest, value0, sizeof(value0));
        if (value1[0] && (parse_u32(value1, &port) != 0 || port == 0u || port > 65535u)) {
            kputs("httpd: invalid TLS port\n"); return;
        }
        if (!value0[0]) str_cpy(value0, "/");
        if (httpd_start_tls((u16)port, value0) != 0) kputs("httpd: failed to start HTTPS listener\n");
        else { kputs("HTTPS server listening on port "); print_u32(port); kputs(" root="); kputs(value0); kputc('\n'); }
        return;
    }
    if (str_cmp(action, "stop") == 0) {
        if (g_user_httpd_pid) {
            (void)ksched_kill(g_user_httpd_pid);
            g_user_httpd_pid = 0u;
            kputs("User-mode HTTP server stopped\n");
        } else if (httpd_stop() != 0) kputs("httpd: already stopped\n");
        else kputs("HTTP server stopped\n");
        return;
    }
    if (str_cmp(action, "proxy") == 0) {
        rest = read_token(rest, value0, sizeof(value0));
        rest = read_token(rest, value1, sizeof(value1));
        (void)read_token(rest, value2, sizeof(value2));
        ip = ip_parse_ipv4(value1, &ok);
        if (!value0[0] || !ok || parse_u32(value2, &port) != 0 || port == 0u || port > 65535u) {
            kputs("Usage: httpd proxy <prefix> <ipv4> <port>\n"); return;
        }
        if (httpd_set_proxy(value0, ip, (u16)port) != 0) kputs("httpd: invalid proxy configuration\n");
        else kputs("HTTP reverse proxy route configured\n");
        return;
    }
    if (str_cmp(action, "noproxy") == 0) { httpd_clear_proxy(); kputs("HTTP reverse proxy disabled\n"); return; }
    kputs("Usage: httpd start <port> [root]|start-tls <port> [root]|stop|status|proxy <prefix> <ip> <port>|noproxy\n");
}

static void cmd_tlsserver(const char* args) {
    char action[16], port_text[16];
    u32 port = 4433u;
    const char* rest = read_token(skip_space(args), action, sizeof(action));

    if (str_cmp(action, "status") == 0) {
        kputs("TLS server: ");
        kputs(tls_server_active() ? "active\n" : "stopped\n");
        kputs("Identity: BearSSL bundled RSA certificate/key (development only)\n");
        return;
    }
    if (str_cmp(action, "start") == 0) {
        rest = read_token(rest, port_text, sizeof(port_text));
        if (port_text[0] && (parse_u32(port_text, &port) != 0 || port == 0u || port > 65535u)) {
            kputs("tlsserver: invalid port\n");
            return;
        }
        if (tls_server_start((u16)port) != 0) {
            kputs("tlsserver: cannot start listener\n");
            return;
        }
        kputs("TLS server listening on TCP port ");
        print_u32(port);
        kputs("\n");
        return;
    }
    if (str_cmp(action, "poll") == 0) {
        if (tls_server_poll() != 0) kputs("tlsserver: no active client or TLS request failed\n");
        else kputs("tlsserver: poll complete\n");
        return;
    }
    if (str_cmp(action, "stop") == 0) {
        if (tls_server_stop() != 0) kputs("tlsserver: already stopped\n");
        else kputs("TLS server stopped\n");
        return;
    }
    kputs("Usage: tlsserver start <port>|poll|stop|status\n");
}

static void cmd_traceroute(const char* args) {
    char host[96], hops_text[12], ip_text[32];
    u32 destination = 0u, max_hops = 16u;
    args = read_token(skip_space(args), host, sizeof(host));
    (void)read_token(args, hops_text, sizeof(hops_text));
    if (!host[0]) { kputs("Usage: traceroute <host|ip> [max-hops]\n"); return; }
    if (hops_text[0] && (parse_u32(hops_text, &max_hops) != 0 || max_hops == 0u || max_hops > 32u)) {
        kputs("traceroute: max-hops must be 1..32\n"); return;
    }
    if (shell_resolve_host(host, &destination) != 0) { kputs("traceroute: name resolution failed\n"); return; }
    kputs("traceroute to "); kputs(host); kputs(" ( "); ip_format_ipv4(destination, ip_text, sizeof(ip_text)); kputs(ip_text); kputs("), "); print_u32(max_hops); kputs(" hops max\n");
    for (u32 hop = 1u; hop <= max_hops; hop++) {
        u32 reply = 0u, elapsed = 0u;
        u8 reached = 0u;
        kputs(" "); print_u32(hop); kputs("  ");
        if (knetstack_traceroute_probe(destination, (u8)hop, (u16)hop, 150u, &reply, &elapsed, &reached) != 0) {
            kputs("*\n");
            continue;
        }
        ip_format_ipv4(reply, ip_text, sizeof(ip_text)); kputs(ip_text); kputs("  ");
        print_u32((elapsed * 1000u) / (ktimer_frequency() ? ktimer_frequency() : 1u)); kputs(" ms\n");
        if (reached) break;
    }
}

/* ────────────────────────────────────────────────────────────
 * Network Management Console — Cisco-style CLI
 * ------------------------------------------------------------
 * Modes: Router> (user), Router# (privileged), Router(config)# (global config)
 * Commands: show, ping, traceroute, dig, enable, conf t, exit, etc.
 */
static void cmd_netconsole(const char* args) {
    char line[128];
    u32 pos;
    int c;
    int is_enable = 0;
    int is_config = 0;
    int running = 1;

    (void)args;

    kputs("\n");
    kputs("╔══════════════════════════════════════════════════════════╗\n");
    kputs("║       LastOS Network Management Console (CLI)           ║\n");
    kputs("║       Cisco-style command-line interface                 ║\n");
    kputs("╠══════════════════════════════════════════════════════════╣\n");
    kputs("║  User mode:     Router>   (limited show + diagnostics)  ║\n");
    kputs("║  Priviliged:    Router#   (full access)                 ║\n");
    kputs("║  Configure:     Router(config)#  (ip/dns/dhcp/nat setup)║\n");
    kputs("║                                                        ║\n");
    kputs("║  Commands:  ?/help     show     ping     traceroute    ║\n");
    kputs("║            dig        arp      enable   configure t    ║\n");
    kputs("║            exit       quit     end                      ║\n");
    kputs("╚══════════════════════════════════════════════════════════╝\n");
    kputs("Type ? for detailed help.\n\n");

    while (running) {
        /* Prompt */
        if (is_config)      kputs("Router(config)# ");
        else if (is_enable) kputs("Router# ");
        else                kputs("Router> ");

        /* Read line */
        pos = 0;
        while (1) {
            c = kgetc();
            if (c == '\r' || c == '\n') { kputc('\n'); break; }
            if (c == 8 || c == 127) {
                if (pos > 0) { pos--; kputs("\b \b"); }
                continue;
            }
            if (c == 27 || c == 3) { /* ESC or Ctrl+C */
                kputs("\n");
                running = 0;
                break;
            }
            if (pos < sizeof(line) - 1) { line[pos++] = (char)c; kputc((char)c); }
        }
        if (!running) break;
        line[pos] = '\0';
        if (!line[0]) continue;

        /* ── Mode switching ── */
        if (str_cmp(line, "exit") == 0 || str_cmp(line, "quit") == 0 || str_cmp(line, "end") == 0) {
            if (is_config) { is_config = 0; kputs("Back to privileged mode\n"); continue; }
            if (is_enable) { is_enable = 0; kputs("Back to user mode\n"); continue; }
            break;
        }
        if (str_cmp(line, "enable") == 0) {
            is_enable = 1;
            kputs("Privileged mode enabled.\n");
            continue;
        }
        if (str_cmp(line, "disable") == 0) {
            is_enable = 0; is_config = 0;
            kputs("User mode.\n");
            continue;
        }
        if (str_cmp(line, "configure terminal") == 0 || str_cmp(line, "conf t") == 0) {
            if (!is_enable) { kputs("Privileged mode required. Use 'enable' first.\n"); continue; }
            is_config = 1;
            kputs("Global configuration mode.\n");
            continue;
        }

        /* ── Help ── */
        if (str_cmp(line, "?") == 0 || str_cmp(line, "help") == 0) {
            kputs("\n");
            kputs("  ┌─────────────────────────────────────────────────────────\n");
            kputs("  │ User-level commands:\n");
            kputs("  │   show           Display system/network information\n");
            kputs("  │   ping <host>    Send ICMP echo requests\n");
            kputs("  │   traceroute <host> [max-hops] Trace route to host\n");
            kputs("  │   dig <host>     DNS resolution query\n");
            kputs("  │   arp            Show ARP table\n");
            kputs("  │   whois <domain> WHOIS lookup\n");
            kputs("  │   curl <url>     Fetch HTTP URL\n");
            kputs("  │   enable         Enter privileged mode\n");
            kputs("  │   exit/quit      Exit the console\n");
            if (is_enable) {
                kputs("  │\n");
                kputs("  │ Privileged commands:\n");
                kputs("  │   show running-config   Display running configuration\n");
                kputs("  │   show interfaces       Interface status\n");
                kputs("  │   show ip route         Routing table\n");
                kputs("  │   show ip dns           DNS server status\n");
                kputs("  │   show dhcp             DHCP server status\n");
                kputs("  │   show nat              NAT status\n");
                kputs("  │   show firewall         Firewall rules\n");
                kputs("  │   show arp              ARP table\n");
                kputs("  │   show ip interface brief  Quick IP summary\n");
                kputs("  │   configure terminal    Enter config mode\n");
                kputs("  │   disable               Return to user mode\n");
            }
            if (is_config) {
                kputs("  │\n");
                kputs("  │ Configuration commands:\n");
                kputs("  │   ip address <ip> <mask>       Set IP address\n");
                kputs("  │   ip gateway <gw>              Set default gateway\n");
                kputs("  │   ip dns-server <ip>           Set DNS server\n");
                kputs("  │   ip forward on|off            IP forwarding\n");
                kputs("  │   dns-server start|stop        Local DNS resolver\n");
                kputs("  │   dhcp-server start|stop       DHCP server\n");
                kputs("  │   nat dnat <tcp|udp> <port> <int_ip> [int_port]\n");
                kputs("  │   nat snat <net> <mask>        Source NAT\n");
                kputs("  │   end/exit                     Back to privileged\n");
            }
            kputs("  └─────────────────────────────────────────────────────────\n");
            continue;
        }

        /* ── show commands ── */
        if (starts_with(line, "show ")) {
            const char* show_arg = skip_space(line + 5);
            if (!show_arg[0]) {
                /* Generic show: show all status */
                cmd_ifconfig(""); kputc('\n');
                cmd_netstack("status"); kputc('\n');
                cmd_net_widget("status");
                continue;
            }
            if (str_cmp(show_arg, "running-config") == 0 || str_cmp(show_arg, "run") == 0) {
                if (!is_enable) { kputs("Privileged mode required.\n"); continue; }
                netstack_status_t st;
                char ipb[32], mb[32], gb[32];
                knetstack_status(&st);
                kputs("! LastOS running configuration\n!\n");
                ip_format_ipv4(st.local_ip, ipb, sizeof(ipb));
                ip_format_ipv4(st.local_mask, mb, sizeof(mb));
                ip_format_ipv4(st.local_gw, gb, sizeof(gb));
                kputs("ip address "); kputs(ipb); kputs(" "); kputs(mb); kputc('\n');
                kputs("ip gateway "); kputs(gb); kputc('\n');
                continue;
            }
            if (str_cmp(show_arg, "interfaces") == 0 || str_cmp(show_arg, "ip interface") == 0) {
                cmd_ifconfig("");
                continue;
            }
            if (str_cmp(show_arg, "ip interface brief") == 0 || str_cmp(show_arg, "ip brief") == 0) {
                netstack_status_t st;
                char ipb[32], mb[32];
                knetstack_status(&st);
                kputs("Interface              IP-Address      Status\n");
                kputs("rtl8139               ");
                if (st.local_ip) {
                    ip_format_ipv4(st.local_ip, ipb, sizeof(ipb));
                    kputs(ipb); kputs("         up\n");
                } else {
                    kputs("unassigned      down\n");
                }
                continue;
            }
            if (str_cmp(show_arg, "ip route") == 0) {
                cmd_netstack("route");
                continue;
            }
            if (str_cmp(show_arg, "ip dns") == 0 || str_cmp(show_arg, "dns-server") == 0) {
                if (!is_enable) { kputs("Privileged mode required.\n"); continue; }
                cmd_dns_server("status");
                continue;
            }
            if (str_cmp(show_arg, "dhcp") == 0 || str_cmp(show_arg, "dhcp-server") == 0) {
                if (!is_enable) { kputs("Privileged mode required.\n"); continue; }
                cmd_dhcp_server("status");
                continue;
            }
            if (str_cmp(show_arg, "nat") == 0) {
                if (!is_enable) { kputs("Privileged mode required.\n"); continue; }
                cmd_nat("status");
                continue;
            }
            if (str_cmp(show_arg, "firewall") == 0 || str_cmp(show_arg, "fw") == 0) {
                if (!is_enable) { kputs("Privileged mode required.\n"); continue; }
                cmd_firewall("list");
                continue;
            }
            if (str_cmp(show_arg, "arp") == 0) {
                cmd_arp("");
                continue;
            }
            if (str_cmp(show_arg, "version") == 0) {
                kputs("LastOS Network Management Console\nKernel Network Stack v1.0\n");
                continue;
            }
            kputs("Unknown show. Try: interfaces, ip route, arp, nat, dhcp, dns-server, running-config\n");
            continue;
        }

        /* ── ping ── */
        if (starts_with(line, "ping ")) {
            cmd_ping(skip_space(line + 5));
            continue;
        }

        /* ── traceroute ── */
        if (starts_with(line, "traceroute ")) {
            cmd_traceroute(skip_space(line + 11));
            continue;
        }

        /* ── dig / nslookup ── */
        if (starts_with(line, "dig ") || starts_with(line, "nslookup ")) {
            const char* q = skip_space(line + 4);
            cmd_dig(q);
            continue;
        }

        /* ── arp ── */
        if (str_cmp(line, "arp") == 0) {
            cmd_arp("");
            continue;
        }

        /* ── whois ── */
        if (starts_with(line, "whois ")) {
            cmd_whois(skip_space(line + 6));
            continue;
        }

        /* ── curl ── */
        if (starts_with(line, "curl ")) {
            cmd_curl(skip_space(line + 5));
            continue;
        }

        /* ── Configuration commands (config mode only) ── */
        if (starts_with(line, "ip address ")) {
            if (!is_config) { kputs("Config mode required. Use 'configure terminal' first.\n"); continue; }
            char t0[32], t1[32];
            int ok = 0;
            const char* rest = skip_space(line + 11);
            rest = read_token(rest, t0, sizeof(t0));
            (void)read_token(rest, t1, sizeof(t1));
            if (!t0[0] || !t1[0]) { kputs("Usage: ip address <ip> <mask>\n"); continue; }
            u32 ip = ip_parse_ipv4(t0, &ok); if (!ok) { kputs("Bad IP\n"); continue; }
            u32 mask = ip_parse_ipv4(t1, &ok); if (!ok) { kputs("Bad mask\n"); continue; }
            knetstack_set_local(ip, mask, netstack_local_gw());
            kputs("IP address configured.\n");
            continue;
        }
        if (starts_with(line, "ip gateway ")) {
            if (!is_config) { kputs("Config mode required.\n"); continue; }
            char t0[32];
            int ok = 0;
            (void)read_token(skip_space(line + 11), t0, sizeof(t0));
            if (!t0[0]) { kputs("Usage: ip gateway <gw>\n"); continue; }
            u32 gw = ip_parse_ipv4(t0, &ok); if (!ok) { kputs("Bad gateway\n"); continue; }
            knetstack_set_local(netstack_local_ip(), netstack_local_mask(), gw);
            kputs("Gateway configured.\n");
            continue;
        }
        if (starts_with(line, "ip dns-server ") || starts_with(line, "ip dns ")) {
            if (!is_config) { kputs("Config mode required.\n"); continue; }
            char t0[32];
            int ok = 0;
            (void)read_token(skip_space(starts_with(line, "ip dns-server ") ? line + 14 : line + 7), t0, sizeof(t0));
            if (!t0[0]) { kputs("Usage: ip dns-server <ip>\n"); continue; }
            u32 ip = ip_parse_ipv4(t0, &ok); if (!ok) { kputs("Bad IP\n"); continue; }
            dns_set_server(0, ip);
            kputs("DNS server configured.\n");
            continue;
        }
        if (starts_with(line, "ip forward ")) {
            if (!is_config) { kputs("Config mode required.\n"); continue; }
            const char* val = skip_space(line + 11);
            if (str_cmp(val, "on") == 0 || str_cmp(val, "enable") == 0) {
                knat_set_ip_forward(1u);
                kputs("IP forwarding enabled.\n");
            } else if (str_cmp(val, "off") == 0 || str_cmp(val, "disable") == 0) {
                knat_set_ip_forward(0u);
                kputs("IP forwarding disabled.\n");
            } else {
                kputs("Usage: ip forward on|off\n");
            }
            continue;
        }
        if (starts_with(line, "dns-server ")) {
            if (!is_config) { kputs("Config mode required.\n"); continue; }
            cmd_dns_server(skip_space(line + 11));
            continue;
        }
        if (starts_with(line, "dhcp-server ")) {
            if (!is_config) { kputs("Config mode required.\n"); continue; }
            cmd_dhcp_server(skip_space(line + 12));
            continue;
        }
        if (starts_with(line, "nat ")) {
            if (!is_config) { kputs("Config mode required.\n"); continue; }
            cmd_nat(skip_space(line + 4));
            continue;
        }

        kputs("Unknown command. Type ? for help.\n");
    }

    kputs("Closing network console.\n");
}

static void cmd_airmesh(const char* args) {
    airmesh_status_t st;
    char sub[16], t0[64];
    args = read_token(skip_space(args), sub, sizeof(sub));

    if (!sub[0] || str_cmp(sub, "status") == 0) {
        airmesh_get_status(&st);
        kputs("AirMesh Experimental L2 Protocol\n");
        kputs("PHY Layer: ");
        if (st.phy_mode == AIRMESH_PHY_ETHERNET) kputs("Ethernet Raw (802.3)\n");
        else if (st.phy_mode == AIRMESH_PHY_WIFI_ADHOC) kputs("Wi-Fi Ad-Hoc (IBSS)\n");
        else kputs("Wi-Fi Mesh (802.11s)\n");

        kputs("Local Node ID: "); print_u32(st.local_node_id);
        kputs(" ("); kputs(st.local_hostname); kputs(")\n");
        kputs("Known Peers: "); print_u32(st.peer_count);
        kputs("\nBeacons TX/RX: "); print_u32(st.beacons_sent);
        kputs("/"); print_u32(st.beacons_received);
        kputs(" | Msg TX/RX: "); print_u32(st.messages_sent);
        kputs("/"); print_u32(st.messages_received);
        kputc('\n');

        for (u32 i = 0; i < AIRMESH_MAX_PEERS; i++) {
            if (st.peers[i].valid) {
                kputs("  [Node "); print_u32(st.peers[i].node_id);
                kputs("] "); kputs(st.peers[i].hostname);
                kputs(" Quality: "); print_u32(st.peers[i].link_quality);
                kputs("% RX/TX: "); print_u32(st.peers[i].packets_rx);
                kputs("/"); print_u32(st.peers[i].packets_tx);
                kputc('\n');
            }
        }
        return;
    }
    if (str_cmp(sub, "beacon") == 0) {
        if (airmesh_send_beacon() == 0) {
            kputs("AirMesh L2 beacon broadcast sent.\n");
        } else {
            kputs("Failed to send AirMesh beacon.\n");
        }
        return;
    }
    if (str_cmp(sub, "send") == 0) {
        args = read_token(args, t0, sizeof(t0));
        u32 target = 0xFFFFFFFFu;
        parse_u32(t0, &target);
        const char* msg = skip_space(args);
        if (!msg[0]) msg = "Hello from LastOS Sovereign Node!";
        if (airmesh_send_text(target, msg) == 0) {
            kputs("AirMesh message transmitted.\n");
        } else {
            kputs("AirMesh transmit failed.\n");
        }
        return;
    }
    kputs("Usage: airmesh status|beacon|send <target_node_id> <msg>\n");
}

