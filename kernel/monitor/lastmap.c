#include "lastmap.h"
#include "kernel_api.h"
#include "scan_table.h"
#include "timing_engine.h"
#include "rate_limiter.h"
#include "multicore_dispatch.h"
#include "syn_scan.h"
#include "connect_scan.h"
#include "udp_scan.h"
#include "banner.h"
#include "resource_engine.h"

typedef struct {
    int active;
    u32 total;
    u32 open;
    u32 closed;
    u32 filtered;
    u32 cursor;
    u32 timer_hz;
    u32 last_tick_print;
    u32 source_ip;
    u32 seq_seed;
    u32 rng;
    u8 randomize;
    u8 banner;
    u8 show_stats;
    timing_profile_t timing;
} lastmap_runtime_t;

static lastmap_runtime_t rt;

static u32 prng_next(void) {
    rt.rng = (rt.rng * 1664525u) + 1013904223u;
    return rt.rng;
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

static void print_port_state(scan_entry_t* e) {
    const char* state = "FILTERED";
    u32 state_pad = 0;
    if (e->state == SCAN_STATE_OPEN) state = "OPEN";
    if (e->state == SCAN_STATE_CLOSED) state = "CLOSED";

    print_u32_local(e->port);
    if (e->port < 10) kputs("       ");
    else if (e->port < 100) kputs("      ");
    else if (e->port < 1000) kputs("     ");
    else if (e->port < 10000) kputs("    ");
    else kputs("   ");

    kputs(state);
    if (e->state == SCAN_STATE_OPEN) state_pad = 6;      /* OPEN (4 chars) */
    else if (e->state == SCAN_STATE_CLOSED) state_pad = 4; /* CLOSED (6 chars) */
    else state_pad = 3; /* FILTERED (8 chars) */
    for (u32 i = 0; i < state_pad; i++) {
        kputc(' ');
    }

    kputs(banner_guess_service(e->port));
    kputc('\n');
}

static void rebuild_counters(void) {
    rt.open = scan_table_state_count(SCAN_STATE_OPEN);
    rt.closed = scan_table_state_count(SCAN_STATE_CLOSED);
    rt.filtered = scan_table_state_count(SCAN_STATE_FILTERED);
}

static int scan_complete(void) {
    return (rt.open + rt.closed + rt.filtered) >= rt.total;
}

static u16 choose_source_port(scan_entry_t* e, u32 idx) {
    u32 base = 40000u + (idx % 20000u);
    if (rt.randomize) {
        base = 1024u + (prng_next() % 54000u);
    } else if (e && (e->scan_type == SCAN_TYPE_CONNECT)) {
        base += 1000u;
    }
    return (u16)base;
}

static int dispatch_probe(scan_entry_t* e, u32 idx, u32 now_tick) {
    if (!e) {
        return -1;
    }
    u16 src_port = choose_source_port(e, idx);
    rt.seq_seed ^= (u32)e->port + now_tick + (src_port << 8);

    int rc = -1;
    if (e->scan_type == SCAN_TYPE_SYN) {
        rc = syn_scan_send(e, rt.source_ip, src_port, rt.seq_seed);
    } else if (e->scan_type == SCAN_TYPE_CONNECT) {
        rc = connect_scan_send(e, rt.source_ip, src_port, rt.seq_seed);
    } else if (e->scan_type == SCAN_TYPE_UDP) {
        rc = udp_scan_send(e, rt.source_ip, src_port);
    }

    if (rc == 0) {
        e->state = SCAN_STATE_SENT;
        e->timestamp = (u64)now_tick;
        e->retries++;
    }
    return rc;
}

static void print_final_report(void) {
    kputs("PORT     STATE     SERVICE\n");
    for (u32 i = 0; i < scan_table_count(); i++) {
        scan_entry_t* e = scan_table_get(i);
        if (!e) {
            continue;
        }
        if (e->state == SCAN_STATE_PENDING || e->state == SCAN_STATE_SENT) {
            e->state = SCAN_STATE_FILTERED;
        }
        print_port_state(e);
        if (rt.banner && e->state == SCAN_STATE_OPEN) {
            banner_on_open_port(e->target_ip, e->port);
        }
    }
    rebuild_counters();
    kputs("Done. open=");
    print_u32_local(rt.open);
    kputs(" closed=");
    print_u32_local(rt.closed);
    kputs(" filtered=");
    print_u32_local(rt.filtered);
    kputc('\n');
}

void lastmap_init(void) {
    rt.active = 0;
    rt.total = 0;
    rt.open = 0;
    rt.closed = 0;
    rt.filtered = 0;
    rt.cursor = 0;
    rt.timer_hz = 100;
    rt.last_tick_print = 0;
    rt.source_ip = 0x0A00020Fu; /* 10.0.2.15 default for QEMU user-net style setups */
    rt.seq_seed = 0x13572468u;
    rt.rng = 0xA5A5A5A5u;
    rt.randomize = 0;
    rt.banner = 0;
    rt.show_stats = 0;
    scan_table_reset();
}

int lastmap_start(const lastmap_config_t* cfg) {
    if (!cfg || cfg->target_ip == 0) {
        return -1;
    }
    if (cfg->port_start == 0 || cfg->port_end == 0 || cfg->port_start > cfg->port_end) {
        return -2;
    }
    if (cfg->scan_type < SCAN_TYPE_SYN || cfg->scan_type > SCAN_TYPE_UDP) {
        return -3;
    }
    if (rt.active) {
        return -4;
    }

    kernel_metrics_t m;
    ksysinfo(&m);
    rt.timer_hz = m.timer_hz ? m.timer_hz : 100;
    timing_engine_build(cfg->timing_template, rt.timer_hz, &rt.timing);
    rate_limiter_init(cfg->rate_pps ? cfg->rate_pps : 100, rt.timer_hz);

    u32 cores = cfg->cores_auto ? kcpuinfo()->logical_cores : cfg->cores;
    if (cfg->cores_auto) {
        u32 workload = (u32)(cfg->port_end - cfg->port_start + 1u);
        if (workload > 1000u) {
            workload = 1000u;
        }
        cores = resource_engine_recommend_threads(workload / 10u, cores, 0);
    }
    if (cores == 0) {
        cores = 1;
    }
    multicore_dispatch_set_cores(cores);

    scan_table_reset();
    for (u32 p = cfg->port_start; p <= cfg->port_end; p++) {
        if (scan_table_add(cfg->target_ip, (u16)p, cfg->scan_type) < 0) {
            break;
        }
        if (p == 65535u) {
            break;
        }
    }

    rt.total = scan_table_count();
    rt.open = 0;
    rt.closed = 0;
    rt.filtered = 0;
    rt.cursor = 0;
    rt.randomize = cfg->randomize;
    rt.banner = cfg->banner || cfg->aggressive;
    rt.show_stats = cfg->show_stats;
    rt.last_tick_print = kticks();
    rt.active = (rt.total > 0) ? 1 : 0;

    if (!rt.active) {
        return -5;
    }

    kputs("LastMap started\n");
    return 0;
}

void lastmap_tick(void) {
    if (!rt.active) {
        return;
    }

    u32 now = kticks();
    u32 n = scan_table_count();
    if (n == 0) {
        rt.active = 0;
        return;
    }

    for (u32 i = 0; i < n; i++) {
        scan_entry_t* e = scan_table_get(i);
        if (!e || e->state != SCAN_STATE_SENT) {
            continue;
        }
        if (((u32)e->timestamp + rt.timing.timeout_ticks) > now) {
            continue;
        }
        if (e->retries < rt.timing.max_retries) {
            e->state = SCAN_STATE_PENDING;
        } else {
            e->state = SCAN_STATE_FILTERED;
        }
    }

    u32 budget = rate_limiter_take_budget();
    for (u32 sent = 0; sent < budget; sent++) {
        scan_entry_t* candidate = 0;
        u32 idx = 0;

        for (u32 probe = 0; probe < n; probe++) {
            idx = (rt.cursor + probe) % n;
            scan_entry_t* e = scan_table_get(idx);
            if (!e || e->state != SCAN_STATE_PENDING) {
                continue;
            }

            (void)multicore_dispatch_pick(e->port, idx);

            if ((u32)e->timestamp + rt.timing.inter_probe_delay_ticks > now) {
                continue;
            }
            candidate = e;
            rt.cursor = (idx + 1) % n;
            break;
        }

        if (!candidate) {
            break;
        }

        dispatch_probe(candidate, idx, now);
    }

    rebuild_counters();
    if (scan_complete()) {
        print_final_report();
        rt.active = 0;
        return;
    }

    if (rt.show_stats && (now - rt.last_tick_print) >= rt.timer_hz) {
        rt.last_tick_print = now;
        lastmap_print_stats();
    }
}

void lastmap_print_stats(void) {
    rebuild_counters();
    kputs("lastmap stats total=");
    print_u32_local(rt.total);
    kputs(" open=");
    print_u32_local(rt.open);
    kputs(" closed=");
    print_u32_local(rt.closed);
    kputs(" filtered=");
    print_u32_local(rt.filtered);
    kputc('\n');
}

void lastmap_on_tcp(u32 src_ip, u16 src_port, u16 dst_port, u8 flags) {
    (void)src_port;
    for (u32 i = 0; i < scan_table_count(); i++) {
        scan_entry_t* e = scan_table_get(i);
        if (!e) {
            continue;
        }
        if (e->target_ip != src_ip || e->port != dst_port) {
            continue;
        }
        if (e->scan_type == SCAN_TYPE_SYN) {
            syn_scan_on_tcp(e, flags);
        } else if (e->scan_type == SCAN_TYPE_CONNECT) {
            connect_scan_on_tcp(e, flags);
        }
    }
}

void lastmap_on_udp(u32 src_ip, u16 src_port, u16 dst_port, const u8* payload, u16 len) {
    (void)src_port;
    for (u32 i = 0; i < scan_table_count(); i++) {
        scan_entry_t* e = scan_table_get(i);
        if (!e) {
            continue;
        }
        if (e->target_ip != src_ip || e->port != dst_port) {
            continue;
        }
        if (e->scan_type == SCAN_TYPE_UDP) {
            udp_scan_on_udp(e, payload, len);
        }
    }
}
