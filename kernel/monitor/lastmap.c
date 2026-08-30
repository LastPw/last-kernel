#include "lastmap.h"
#include "kernel_api.h"
#include "ip.h"
#include "string.h"
#include "timer.h"
#include "tcp_raw.h"
#include "udp_raw.h"
#include "resource_engine.h"

/* --- Scan Table Subsystem --- */
#define SCAN_TABLE_MAX 2048

static scan_entry_t s_scan_table[SCAN_TABLE_MAX];
static u32 s_scan_table_count = 0;

void scan_table_reset(void) {
    s_scan_table_count = 0;
    for (u32 i = 0; i < SCAN_TABLE_MAX; i++) {
        s_scan_table[i].target_ip = 0;
        s_scan_table[i].port = 0;
        s_scan_table[i].scan_type = SCAN_TYPE_SYN;
        s_scan_table[i].state = SCAN_STATE_PENDING;
        s_scan_table[i].timestamp = 0;
        s_scan_table[i].retries = 0;
        s_scan_table[i].response_flags = 0;
    }
}

int scan_table_insert(u32 target_ip, u16 port, u8 scan_type) {
    if (s_scan_table_count >= SCAN_TABLE_MAX) return -1;
    u32 idx = s_scan_table_count++;
    s_scan_table[idx].target_ip = target_ip;
    s_scan_table[idx].port = port;
    s_scan_table[idx].scan_type = scan_type;
    s_scan_table[idx].state = SCAN_STATE_PENDING;
    s_scan_table[idx].timestamp = 0;
    s_scan_table[idx].retries = 0;
    s_scan_table[idx].response_flags = 0;
    return 0;
}

u32 scan_table_count(void) { return s_scan_table_count; }

scan_entry_t* scan_table_get(u32 index) {
    if (index >= s_scan_table_count) return (void*)0;
    return &s_scan_table[index];
}

scan_entry_t* scan_table_find_port(u16 port) {
    for (u32 i = 0; i < s_scan_table_count; i++) {
        if (s_scan_table[i].port == port) return &s_scan_table[i];
    }
    return (void*)0;
}

u32 scan_table_state_count(u8 state) {
    u32 c = 0;
    for (u32 i = 0; i < s_scan_table_count; i++) {
        if (s_scan_table[i].state == state) c++;
    }
    return c;
}

/* --- Timing Engine & Rate Limiter --- */
void timing_engine_build(u8 template_id, u32 timer_hz, timing_profile_t* out) {
    if (!out) return;
    if (template_id == 0) {
        out->timeout_ticks = timer_hz * 5;
        out->max_retries = 3;
        out->inter_probe_delay_ticks = timer_hz;
    } else if (template_id == 1) {
        out->timeout_ticks = timer_hz * 3;
        out->max_retries = 2;
        out->inter_probe_delay_ticks = timer_hz / 2;
    } else if (template_id == 2) {
        out->timeout_ticks = timer_hz * 2;
        out->max_retries = 2;
        out->inter_probe_delay_ticks = timer_hz / 4;
    } else if (template_id == 3) {
        out->timeout_ticks = timer_hz;
        out->max_retries = 1;
        out->inter_probe_delay_ticks = 1;
    } else if (template_id == 4 || template_id == 5) {
        out->timeout_ticks = timer_hz / 2;
        if (out->timeout_ticks == 0) out->timeout_ticks = 1;
        out->max_retries = 1;
        out->inter_probe_delay_ticks = 1;
    }
}

static u32 s_rate_bucket = 0;
static u32 s_rate_per_tick = 0;
static u32 s_rate_max_bucket = 0;

void rate_limiter_init(u32 packets_per_sec, u32 timer_hz) {
    if (!packets_per_sec) packets_per_sec = 100;
    if (!timer_hz) timer_hz = 100;
    s_rate_per_tick = (packets_per_sec << 16) / timer_hz;
    if (!s_rate_per_tick) s_rate_per_tick = 1;
    s_rate_max_bucket = (packets_per_sec << 16);
    s_rate_bucket = s_rate_max_bucket;
}

u32 rate_limiter_take_budget(void) {
    s_rate_bucket += s_rate_per_tick;
    if (s_rate_bucket > s_rate_max_bucket) s_rate_bucket = s_rate_max_bucket;
    u32 budget = s_rate_bucket >> 16;
    if (budget > 64) budget = 64;
    s_rate_bucket -= (budget << 16);
    return budget;
}

/* --- Multicore Dispatch --- */
static u32 s_active_cores = 1;
void multicore_dispatch_set_cores(u32 cores) {
    if (!cores) cores = 1;
    if (cores > 32) cores = 32;
    s_active_cores = cores;
}

/* --- Probe Transmitters --- */
int syn_scan_send(scan_entry_t* e, u32 source_ip, u16 source_port, u32 seq_seed) {
    if (!e) return -1;
    return tcp_raw_send_syn(source_ip, e->target_ip, source_port, e->port, seq_seed);
}

int connect_scan_send(scan_entry_t* e, u32 source_ip, u16 source_port, u32 seq_seed) {
    if (!e) return -1;
    return tcp_raw_send_syn(source_ip, e->target_ip, source_port, e->port, seq_seed);
}

int udp_scan_send(scan_entry_t* e, u32 source_ip, u16 source_port) {
    if (!e) return -1;
    u8 payload[1] = { 0 };
    return udp_raw_send(source_ip, e->target_ip, source_port, e->port, payload, 0);
}

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
    if (port == 5432) return "postgres";
    if (port == 8080) return "http-proxy";
    return "unknown";
}

void banner_on_open_port(u32 target_ip, u16 port) {
    (void)target_ip; (void)port;
}

/* --- LastMap Engine Runtime --- */
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
    if (value == 0) { kputc('0'); return; }
    while (value > 0 && i < (int)sizeof(buf) - 1) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) kputc(buf[--i]);
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
    if (e->state == SCAN_STATE_OPEN) state_pad = 6;
    else if (e->state == SCAN_STATE_CLOSED) state_pad = 4;
    else state_pad = 3;
    for (u32 i = 0; i < state_pad; i++) kputc(' ');

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
    if (!e) return -1;
    u16 src_port = choose_source_port(e, idx);
    rt.seq_seed ^= (u32)e->port + now_tick + (src_port << 8);

    int rc = -1;
    if (e->scan_type == SCAN_TYPE_SYN) rc = syn_scan_send(e, rt.source_ip, src_port, rt.seq_seed);
    else if (e->scan_type == SCAN_TYPE_CONNECT) rc = connect_scan_send(e, rt.source_ip, src_port, rt.seq_seed);
    else if (e->scan_type == SCAN_TYPE_UDP) rc = udp_scan_send(e, rt.source_ip, src_port);

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
        if (!e) continue;
        if (e->state == SCAN_STATE_PENDING || e->state == SCAN_STATE_SENT) e->state = SCAN_STATE_FILTERED;
        print_port_state(e);
        if (rt.banner && e->state == SCAN_STATE_OPEN) banner_on_open_port(e->target_ip, e->port);
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
    rt.source_ip = 0x0A00020Fu;
    rt.seq_seed = 0x13572468u;
    rt.rng = 0xA5A5A5A5u;
    rt.randomize = 0;
    rt.banner = 0;
    rt.show_stats = 0;
    scan_table_reset();
}

int lastmap_start(const lastmap_config_t* cfg) {
    if (!cfg || cfg->target_ip == 0) return -1;
    if (cfg->port_start == 0 || cfg->port_end == 0 || cfg->port_start > cfg->port_end) return -2;
    if (cfg->scan_type < SCAN_TYPE_SYN || cfg->scan_type > SCAN_TYPE_UDP) return -3;
    if (rt.active) return -4;

    kernel_metrics_t m;
    ksysinfo(&m);
    rt.timer_hz = m.timer_hz ? m.timer_hz : 100;
    timing_engine_build(cfg->timing_template, rt.timer_hz, &rt.timing);
    rate_limiter_init(cfg->rate_pps ? cfg->rate_pps : 100, rt.timer_hz);

    u32 cores = cfg->cores_auto ? kcpuinfo()->logical_cores : cfg->cores;
    if (cfg->cores_auto) {
        u32 workload = (u32)(cfg->port_end - cfg->port_start + 1u);
        if (workload > 1000u) workload = 1000u;
        cores = resource_engine_recommend_threads(workload / 10u, cores, 0);
    }
    if (cores == 0) cores = 1;
    multicore_dispatch_set_cores(cores);

    scan_table_reset();
    for (u32 p = cfg->port_start; p <= cfg->port_end; p++) {
        if (scan_table_insert(cfg->target_ip, (u16)p, cfg->scan_type) != 0) break;
    }

    rt.total = scan_table_count();
    rt.open = 0;
    rt.closed = 0;
    rt.filtered = 0;
    rt.cursor = 0;
    rt.randomize = cfg->randomize;
    rt.banner = cfg->banner;
    rt.show_stats = cfg->show_stats;
    rt.active = 1;

    kputs("Starting lastmap scan on ");
    print_u32_local((cfg->target_ip >> 24) & 0xFF); kputc('.');
    print_u32_local((cfg->target_ip >> 16) & 0xFF); kputc('.');
    print_u32_local((cfg->target_ip >> 8) & 0xFF); kputc('.');
    print_u32_local(cfg->target_ip & 0xFF);
    kputs(" (ports ");
    print_u32_local(cfg->port_start);
    kputs("-");
    print_u32_local(cfg->port_end);
    kputs(")\n");
    return 0;
}

void lastmap_tick(void) {
    if (!rt.active) return;
    u32 now_tick = timer_ticks();
    u32 budget = rate_limiter_take_budget();

    while (budget > 0 && rt.cursor < rt.total) {
        scan_entry_t* e = scan_table_get(rt.cursor);
        if (e && e->state == SCAN_STATE_PENDING) {
            if (dispatch_probe(e, rt.cursor, now_tick) == 0) budget--;
        }
        rt.cursor++;
    }

    for (u32 i = 0; i < rt.total; i++) {
        scan_entry_t* e = scan_table_get(i);
        if (!e || e->state != SCAN_STATE_SENT) continue;
        if ((now_tick - (u32)e->timestamp) >= rt.timing.timeout_ticks) {
            if (e->retries < rt.timing.max_retries && budget > 0) {
                if (dispatch_probe(e, i, now_tick) == 0) budget--;
            } else {
                e->state = SCAN_STATE_FILTERED;
                rt.filtered++;
            }
        }
    }

    if (scan_complete()) {
        rt.active = 0;
        print_final_report();
    }
}

void lastmap_print_stats(void) {
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
    (void)src_ip; (void)dst_port;
    if (!rt.active) return;
    scan_entry_t* e = scan_table_find_port(src_port);
    if (!e || e->state != SCAN_STATE_SENT) return;
    e->response_flags = flags;
    if ((flags & 0x12) == 0x12) { e->state = SCAN_STATE_OPEN; rt.open++; }
    else if (flags & 0x04) { e->state = SCAN_STATE_CLOSED; rt.closed++; }
    if (scan_complete()) { rt.active = 0; print_final_report(); }
}

void lastmap_on_udp(u32 src_ip, u16 src_port, u16 dst_port, const u8* payload, u16 len) {
    (void)src_ip; (void)dst_port; (void)payload;
    if (!rt.active) return;
    scan_entry_t* e = scan_table_find_port(src_port);
    if (!e || e->state != SCAN_STATE_SENT) return;
    if (len > 0) { e->state = SCAN_STATE_OPEN; rt.open++; }
    else { e->state = SCAN_STATE_CLOSED; rt.closed++; }
    if (scan_complete()) { rt.active = 0; print_final_report(); }
}

int lastmap_command(const char* args) {
    (void)args;
    return 0;
}
