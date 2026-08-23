#include "lastmap.h"
#include "kernel_api.h"
#include "scan_table.h"
#include "ip.h"
#include "string.h"

static const char* skip_space(const char* s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static void print_u32_local(u32 value) {
    char buf[16];
    int i = 0;
    if (value == 0) {
        kputc('0');
        return;
    }
    while (value > 0 && i < (int)sizeof(buf) - 1) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        kputc(buf[--i]);
    }
}

static int parse_u32_local(const char* s, u32* out) {
    u32 v = 0;
    int seen = 0;
    while (*s >= '0' && *s <= '9') {
        v = (v * 10u) + (u32)(*s - '0');
        s++;
        seen = 1;
    }
    if (!seen) {
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_port_range(const char* text, u16* out_start, u16* out_end) {
    u32 a = 0;
    u32 b = 0;
    if (parse_u32_local(text, &a) != 0 || a == 0 || a > 65535u) {
        return -1;
    }
    while (*text >= '0' && *text <= '9') {
        text++;
    }
    if (*text != '-') {
        *out_start = (u16)a;
        *out_end = (u16)a;
        return 0;
    }
    text++;
    if (parse_u32_local(text, &b) != 0 || b == 0 || b > 65535u || b < a) {
        return -1;
    }
    *out_start = (u16)a;
    *out_end = (u16)b;
    return 0;
}

static int tokenize(char* buf, const char** toks, u32 max_tok) {
    u32 n = 0;
    char* p = buf;

    while (*p && n < max_tok) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }
        toks[n++] = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    return (int)n;
}

static void print_help(void) {
    kputs("lastmap <target_ip>\n");
    kputs("lastmap -p 1-1000 <target_ip>\n");
    kputs("lastmap -sS|-sT|-sU <target_ip>\n");
    kputs("lastmap -A <target_ip>\n");
    kputs("lastmap -T0..-T5 <target_ip>\n");
    kputs("lastmap --randomize --rate <pps> --cores auto|n --banner --stats <target_ip>\n");
}

int lastmap_command(const char* args) {
    lastmap_config_t cfg;
    cfg.target_ip = 0;
    cfg.port_start = 1;
    cfg.port_end = 1024;
    cfg.scan_type = SCAN_TYPE_SYN;
    cfg.timing_template = 3;
    cfg.randomize = 0;
    cfg.banner = 0;
    cfg.show_stats = 0;
    cfg.aggressive = 0;
    cfg.cores_auto = 1;
    cfg.cores = 1;
    cfg.rate_pps = 300;
    int adv_enabled = 0;

    args = skip_space(args);
    if (!args[0]) {
        print_help();
        return 0;
    }

    if (str_cmp(args, "--stats") == 0) {
        lastmap_print_stats();
        return 0;
    }

    char buf[192];
    str_ncpy(buf, args, sizeof(buf));
    const char* tok[24];
    int n = tokenize(buf, tok, 24);
    if (n <= 0) {
        print_help();
        return 0;
    }

    for (int i = 0; i < n; i++) {
        if (str_cmp(tok[i], "--adv") == 0) {
            adv_enabled = 1;
        } else if (tok[i][0] == '-' && !adv_enabled) {
            kputs("Advanced flags are hidden. Add --adv first.\n");
            return -1;
        } else if (str_cmp(tok[i], "-sS") == 0) {
            cfg.scan_type = SCAN_TYPE_SYN;
        } else if (str_cmp(tok[i], "-sT") == 0) {
            cfg.scan_type = SCAN_TYPE_CONNECT;
        } else if (str_cmp(tok[i], "-sU") == 0) {
            cfg.scan_type = SCAN_TYPE_UDP;
        } else if (str_cmp(tok[i], "-A") == 0) {
            cfg.aggressive = 1;
            cfg.banner = 1;
            cfg.scan_type = SCAN_TYPE_SYN;
            cfg.port_start = 1;
            cfg.port_end = 2048;
        } else if (str_ncmp(tok[i], "-T", 2) == 0) {
            u32 tv = 3;
            if (parse_u32_local(tok[i] + 2, &tv) == 0 && tv <= 5) {
                cfg.timing_template = (u8)tv;
            }
        } else if (str_cmp(tok[i], "-p") == 0) {
            if ((i + 1) >= n || parse_port_range(tok[i + 1], &cfg.port_start, &cfg.port_end) != 0) {
                kputs("Invalid port range.\n");
                return -1;
            }
            i++;
        } else if (str_cmp(tok[i], "--randomize") == 0) {
            cfg.randomize = 1;
        } else if (str_cmp(tok[i], "--banner") == 0) {
            cfg.banner = 1;
        } else if (str_cmp(tok[i], "--stats") == 0) {
            cfg.show_stats = 1;
        } else if (str_cmp(tok[i], "--rate") == 0) {
            u32 r = 0;
            if ((i + 1) >= n || parse_u32_local(tok[i + 1], &r) != 0) {
                kputs("Invalid rate.\n");
                return -1;
            }
            cfg.rate_pps = r;
            i++;
        } else if (str_cmp(tok[i], "--cores") == 0) {
            if ((i + 1) >= n) {
                kputs("Invalid --cores value.\n");
                return -1;
            }
            if (str_cmp(tok[i + 1], "auto") == 0) {
                cfg.cores_auto = 1;
                cfg.cores = 1;
            } else {
                u32 c = 0;
                if (parse_u32_local(tok[i + 1], &c) != 0 || c == 0 || c > 32) {
                    kputs("Invalid core count.\n");
                    return -1;
                }
                cfg.cores_auto = 0;
                cfg.cores = (u8)c;
            }
            i++;
        } else {
            int ok = 0;
            u32 ip = ip_parse_ipv4(tok[i], &ok);
            if (ok) {
                cfg.target_ip = ip;
            } else {
                kputs("Unknown option: ");
                kputs(tok[i]);
                kputc('\n');
                print_help();
                return -1;
            }
        }
    }

    if (cfg.target_ip == 0) {
        kputs("Target IP required.\n");
        print_help();
        return -1;
    }

    int rc = lastmap_start(&cfg);
    if (rc != 0) {
        kputs("lastmap start failed, code=");
        print_u32_local((u32)(rc * -1));
        kputc('\n');
        return rc;
    }

    return 0;
}
