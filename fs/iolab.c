#include "iolab.h"
#include "io_scheduler.h"
#include "netstack.h"
#include "timer.h"

#define IOLAB_RING 256u

typedef struct {
    u32 tick;
    io_stats_t io;
    netstack_status_t ns;
    u32 throughput_score;
    u32 latency_score;
    u32 pressure_score;
    u32 risk_score;
    u32 health_score;
    u32 mode_hint;
} iolab_sample_t;

typedef struct {
    iolab_sample_t ring[IOLAB_RING];
    u32 head;
    u32 count;
    u32 ioq_peak;
    u32 pressure;
    u32 risk;
    u32 health;
    u32 throughput;
    u32 latency;
    u32 mode_hint;
} iolab_state_t;

static iolab_state_t g;

static u32 clamp100(u32 v) {
    return (v > 100u) ? 100u : v;
}

static u32 avg_recent_u32(u32 (*f)(const iolab_sample_t*), u32 max_n) {
    if (g.count == 0u) return 0u;
    u32 n = g.count;
    if (n > max_n) n = max_n;
    if (n == 0u) return 0u;
    u32 sum = 0u;
    for (u32 i = 0u; i < n; i++) {
        u32 idx = (g.head + IOLAB_RING - 1u - i) % IOLAB_RING;
        sum += f(&g.ring[idx]);
    }
    return sum / n;
}

static u32 f_ioq(const iolab_sample_t* s) { return s->io.queue_depth; }
static u32 f_tp(const iolab_sample_t* s) { return s->throughput_score; }
static u32 f_lat(const iolab_sample_t* s) { return s->latency_score; }

static void recompute(iolab_sample_t* s) {
    u32 ioq = s->io.queue_depth;
    u32 drop = s->io.dropped_ops;
    u32 malformed = s->ns.malformed;
    u32 sockets = s->ns.sockets_open;

    u32 throughput = clamp100((sockets * 6u) + (s->io.net_packets / 8u) + (s->io.ops_total / 64u));
    u32 latency = 100u;
    if (ioq > 4u) {
        u32 p = (ioq - 4u) * 8u;
        if (p > 95u) p = 95u;
        latency = 100u - p;
    }
    if (drop > 0u) {
        u32 pd = (drop > 40u) ? 40u : drop;
        latency = (latency > pd) ? (latency - pd) : 0u;
    }

    u32 pressure = clamp100((ioq * 5u) + (drop * 2u) + (malformed > 30u ? 20u : malformed / 2u));
    u32 risk = pressure / 2u;
    if (malformed > 0u) risk += 10u;
    if (drop > 0u) risk += 10u;
    if (latency < 40u) risk += 20u;
    risk = clamp100(risk);

    u32 hint = 2u; /* adaptive */
    if (pressure > 70u || latency < 35u) hint = 0u; /* latency mode */
    else if (throughput > 70u && pressure < 50u) hint = 1u; /* throughput mode */

    g.throughput = throughput;
    g.latency = latency;
    g.pressure = pressure;
    g.risk = risk;
    g.health = 100u - risk;
    g.mode_hint = hint;

    s->throughput_score = throughput;
    s->latency_score = latency;
    s->pressure_score = pressure;
    s->risk_score = risk;
    s->health_score = g.health;
    s->mode_hint = hint;
}

void iolab_init(void) {
    g.head = 0u;
    g.count = 0u;
    g.ioq_peak = 0u;
    g.pressure = 0u;
    g.risk = 0u;
    g.health = 100u;
    g.throughput = 0u;
    g.latency = 100u;
    g.mode_hint = 2u;
    for (u32 i = 0u; i < IOLAB_RING; i++) {
        g.ring[i].tick = 0u;
    }
}

void iolab_reset(void) {
    iolab_init();
}

void iolab_tick(void) {
    iolab_sample_t s;
    s.tick = timer_ticks();
    io_scheduler_stats(&s.io);
    netstack_status(&s.ns);
    if (s.io.queue_depth > g.ioq_peak) g.ioq_peak = s.io.queue_depth;
    recompute(&s);

    g.ring[g.head] = s;
    g.head = (g.head + 1u) % IOLAB_RING;
    if (g.count < IOLAB_RING) g.count++;
}

void iolab_snapshot(iolab_snapshot_t* out) {
    if (!out) return;
    iolab_sample_t s;
    if (g.count == 0u) {
        s.tick = timer_ticks();
        io_scheduler_stats(&s.io);
        netstack_status(&s.ns);
        recompute(&s);
    } else {
        s = g.ring[(g.head + IOLAB_RING - 1u) % IOLAB_RING];
    }

    out->samples = g.count;
    out->window = IOLAB_RING;
    out->io_ops_total = s.io.ops_total;
    out->io_queue_depth_avg = avg_recent_u32(f_ioq, 64u);
    out->io_queue_depth_peak = g.ioq_peak;
    out->io_drop_total = s.io.dropped_ops;
    out->net_packets_total = s.io.net_packets;
    out->net_frames_rx = s.ns.frames_rx;
    out->net_malformed_total = s.ns.malformed;
    out->net_sockets_open = s.ns.sockets_open;
    out->mode_hint = g.mode_hint;
    out->throughput_score = avg_recent_u32(f_tp, 64u);
    out->latency_score = avg_recent_u32(f_lat, 64u);
    out->pressure_score = g.pressure;
    out->risk_score = g.risk;
    out->health_score = g.health;
    out->last_tick = s.tick;
}

int iolab_validate(iolab_validation_t* out) {
    iolab_validation_t r;
    r.checks_total = 0u;
    r.checks_passed = 0u;
    r.checks_failed = 0u;
    r.first_error = 0u;

    #define IOLAB_CHECK(cond, err) do { \
        r.checks_total++; \
        if (cond) r.checks_passed++; \
        else { \
            r.checks_failed++; \
            if (r.first_error == 0u) r.first_error = (err); \
        } \
    } while (0)

    IOLAB_CHECK(g.head < IOLAB_RING, 3001u);
    IOLAB_CHECK(g.count <= IOLAB_RING, 3002u);
    IOLAB_CHECK(g.pressure <= 100u, 3003u);
    IOLAB_CHECK(g.risk <= 100u, 3004u);
    IOLAB_CHECK(g.health <= 100u, 3005u);
    IOLAB_CHECK(g.mode_hint <= 2u, 3006u);

    if (g.count > 1u) {
        u32 first = (g.head + IOLAB_RING - g.count) % IOLAB_RING;
        u32 prev = g.ring[first].tick;
        for (u32 i = 1u; i < g.count; i++) {
            u32 idx = (first + i) % IOLAB_RING;
            IOLAB_CHECK(g.ring[idx].tick >= prev, 3007u);
            prev = g.ring[idx].tick;
        }
    } else {
        IOLAB_CHECK(1, 0u);
    }

    if (out) *out = r;
    return (r.checks_failed == 0u) ? 0 : -1;
}

int iolab_run(u32 mode, u32 rounds, u32 payload, iolab_run_result_t* out) {
    if (rounds < 32u) rounds = 32u;
    if (rounds > 4096u) rounds = 4096u;
    if (payload < 64u) payload = 64u;
    if (payload > 8192u) payload = 8192u;

    io_stats_t before_io;
    io_scheduler_stats(&before_io);
    iolab_run_result_t r;
    r.mode = mode;
    r.rounds = rounds;
    r.ops_before = before_io.ops_total;
    r.ops_after = before_io.ops_total;
    r.drops_before = before_io.dropped_ops;
    r.drops_after = before_io.dropped_ops;
    r.packets_before = before_io.net_packets;
    r.packets_after = before_io.net_packets;
    r.pressure_before = g.pressure;
    r.pressure_after = g.pressure;
    r.failures = 0u;

    io_mode_t saved = io_scheduler_mode();
    if (mode == 0u) io_scheduler_set_mode(IO_MODE_LATENCY);
    else if (mode == 1u) io_scheduler_set_mode(IO_MODE_THROUGHPUT);
    else io_scheduler_set_mode(IO_MODE_ADAPTIVE);

    for (u32 i = 0u; i < rounds; i++) {
        u32 bytes = payload + ((i & 15u) * 64u);
        u32 task = 1u + (i % 4u);
        if (io_scheduler_submit(task, bytes, (i & 1u) == 0u) != 0) {
            r.failures++;
        }
        if ((i % 3u) == 0u) {
            io_scheduler_account_net_packet();
        }
        if ((i % 2u) == 0u) {
            io_scheduler_tick(timer_ticks());
        }
        if ((i % 5u) == 0u) {
            iolab_tick();
        }
    }
    iolab_tick();
    io_scheduler_set_mode(saved);

    io_stats_t after_io;
    io_scheduler_stats(&after_io);
    r.ops_after = after_io.ops_total;
    r.drops_after = after_io.dropped_ops;
    r.packets_after = after_io.net_packets;
    r.pressure_after = g.pressure;
    if (out) *out = r;
    return (r.failures == 0u) ? 0 : -1;
}
