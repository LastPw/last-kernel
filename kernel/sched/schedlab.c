#include "schedlab.h"
#include "scheduler.h"
#include "timer.h"

#define SL_RING 256u
#define SL_TASK_MAX 64u

typedef struct {
    u32 tick;
    u32 task_id;
    u32 core_id;
    u32 busy;
    u32 runqueue_depth;
    u32 slice_goal;
    u32 quantum_level;
    u32 switches_total;
} sched_sample_t;

typedef struct {
    sched_sample_t ring[SL_RING];
    u32 head;
    u32 count;
    u32 run_ticks[SL_TASK_MAX];
    u32 switch_in[SL_TASK_MAX];
    u32 switches_total;
    u32 runqueue_peak;
    u32 fairness_score;
    u32 starvation_score;
    u32 pressure_score;
    u32 risk_score;
    u32 health_score;
} schedlab_state_t;

static schedlab_state_t g;

static u32 clamp100(u32 v) {
    return (v > 100u) ? 100u : v;
}

static u32 task_slot(u32 task_id) {
    return task_id % SL_TASK_MAX;
}

static u32 jain_fairness_score(void) {
    u32 sum = 0u;
    u32 sq = 0u;
    u32 n = 0u;
    for (u32 i = 0u; i < SL_TASK_MAX; i++) {
        u32 x = g.run_ticks[i];
        if (x == 0u) continue;
        /* Normalize to avoid large intermediate math in freestanding 32-bit build. */
        x = (x >> 4u) + 1u;
        if (x > 1024u) x = 1024u;
        n++;
        if (sum < 60000u) sum += x;
        u32 xx = x * x;
        if (sq < 4000000000u - xx) sq += xx;
    }
    if (n <= 1u || sq == 0u) return 100u;
    u32 num = sum * sum;
    if (num == 0u) return 0u;
    u32 den = n * sq;
    if (den == 0u) return 100u;
    if (den >= 100u) return clamp100(num / (den / 100u));
    return clamp100((num * 100u) / den);
}

static u32 starvation_score(void) {
    u32 active = 0u;
    u32 starved = 0u;
    for (u32 i = 0u; i < SL_TASK_MAX; i++) {
        if (g.switch_in[i] == 0u && g.run_ticks[i] > 0u) {
            active++;
            continue;
        }
        if (g.switch_in[i] > 0u || g.run_ticks[i] > 0u) {
            active++;
            if (g.switch_in[i] <= 1u && g.run_ticks[i] > 64u) {
                starved++;
            }
        }
    }
    if (active == 0u) return 0u;
    return (starved * 100u) / active;
}

static u32 avg_recent_u32(u32 (*f)(const sched_sample_t*), u32 max_n) {
    if (g.count == 0u) return 0u;
    u32 n = g.count;
    if (n > max_n) n = max_n;
    if (n == 0u) return 0u;
    u32 sum = 0u;
    for (u32 i = 0u; i < n; i++) {
        u32 idx = (g.head + SL_RING - 1u - i) % SL_RING;
        sum += f(&g.ring[idx]);
    }
    return sum / n;
}

static u32 f_runq(const sched_sample_t* s) { return s->runqueue_depth; }
static u32 f_slice(const sched_sample_t* s) { return s->slice_goal; }
static u32 f_quant(const sched_sample_t* s) { return s->quantum_level; }

static u32 switch_rate_per_100_ticks(void) {
    if (g.count < 2u) return 0u;
    u32 first = (g.head + SL_RING - g.count) % SL_RING;
    u32 last = (g.head + SL_RING - 1u) % SL_RING;
    u32 dt = g.ring[last].tick - g.ring[first].tick;
    if (dt == 0u) return 0u;
    u32 ds = g.ring[last].switches_total - g.ring[first].switches_total;
    return (ds * 100u) / dt;
}

static void recompute_scores(void) {
    g.fairness_score = clamp100(jain_fairness_score());
    g.starvation_score = clamp100(starvation_score());

    u32 runq = avg_recent_u32(f_runq, 64u);
    u32 swr = switch_rate_per_100_ticks();
    u32 pressure = (runq * 5u);
    if (pressure > 100u) pressure = 100u;
    pressure = clamp100((pressure + swr) / 2u);
    g.pressure_score = pressure;

    u32 risk = 0u;
    if (g.fairness_score < 50u) risk += (50u - g.fairness_score);
    risk += g.starvation_score / 2u;
    risk += pressure / 3u;
    g.risk_score = clamp100(risk);
    g.health_score = 100u - g.risk_score;
}

void schedlab_init(void) {
    g.head = 0u;
    g.count = 0u;
    g.switches_total = 0u;
    g.runqueue_peak = 0u;
    g.fairness_score = 100u;
    g.starvation_score = 0u;
    g.pressure_score = 0u;
    g.risk_score = 0u;
    g.health_score = 100u;
    for (u32 i = 0u; i < SL_TASK_MAX; i++) {
        g.run_ticks[i] = 0u;
        g.switch_in[i] = 0u;
    }
    for (u32 i = 0u; i < SL_RING; i++) {
        g.ring[i].tick = 0u;
        g.ring[i].task_id = 0u;
        g.ring[i].core_id = 0u;
        g.ring[i].busy = 0u;
        g.ring[i].runqueue_depth = 0u;
        g.ring[i].slice_goal = 0u;
        g.ring[i].quantum_level = 0u;
        g.ring[i].switches_total = 0u;
    }
}

void schedlab_reset(void) {
    schedlab_init();
}

void schedlab_tick(u32 current_task_id, u32 core_id, u32 is_busy, u32 runqueue_depth, u32 slice_goal, u32 quantum_level) {
    sched_sample_t s;
    s.tick = timer_ticks();
    s.task_id = current_task_id;
    s.core_id = core_id;
    s.busy = is_busy ? 1u : 0u;
    s.runqueue_depth = runqueue_depth;
    s.slice_goal = slice_goal;
    s.quantum_level = quantum_level;
    s.switches_total = g.switches_total;

    if (runqueue_depth > g.runqueue_peak) g.runqueue_peak = runqueue_depth;
    if (is_busy) {
        u32 slot = task_slot(current_task_id);
        if (g.run_ticks[slot] < 0xFFFFFFFEu) g.run_ticks[slot]++;
    }

    g.ring[g.head] = s;
    g.head = (g.head + 1u) % SL_RING;
    if (g.count < SL_RING) g.count++;

    recompute_scores();
}

void schedlab_on_switch(u32 from_task, u32 to_task, u32 core_id) {
    (void)from_task;
    (void)core_id;
    if (g.switches_total < 0xFFFFFFFEu) g.switches_total++;
    u32 slot = task_slot(to_task);
    if (g.switch_in[slot] < 0xFFFFFFFEu) g.switch_in[slot]++;
}

void schedlab_snapshot(schedlab_snapshot_t* out) {
    if (!out) return;
    out->samples = g.count;
    out->window = SL_RING;
    out->active_tasks = scheduler_task_count();
    out->runqueue_avg = avg_recent_u32(f_runq, 64u);
    out->runqueue_peak = g.runqueue_peak;
    out->switches_total = g.switches_total;
    out->switch_rate = switch_rate_per_100_ticks();
    out->slice_goal_avg = avg_recent_u32(f_slice, 64u);
    out->quantum_level_avg = avg_recent_u32(f_quant, 64u);
    out->fairness_score = g.fairness_score;
    out->starvation_score = g.starvation_score;
    out->pressure_score = g.pressure_score;
    out->risk_score = g.risk_score;
    out->health_score = g.health_score;
    if (g.count == 0u) out->last_tick = 0u;
    else out->last_tick = g.ring[(g.head + SL_RING - 1u) % SL_RING].tick;
}

int schedlab_validate(schedlab_validation_t* out) {
    schedlab_validation_t r;
    r.checks_total = 0u;
    r.checks_passed = 0u;
    r.checks_failed = 0u;
    r.first_error = 0u;

    #define SL_CHECK(cond, err) do { \
        r.checks_total++; \
        if (cond) r.checks_passed++; \
        else { \
            r.checks_failed++; \
            if (r.first_error == 0u) r.first_error = (err); \
        } \
    } while (0)

    SL_CHECK(g.head < SL_RING, 2001u);
    SL_CHECK(g.count <= SL_RING, 2002u);
    SL_CHECK(g.fairness_score <= 100u, 2003u);
    SL_CHECK(g.risk_score <= 100u, 2004u);
    SL_CHECK(g.health_score <= 100u, 2005u);
    SL_CHECK(g.pressure_score <= 100u, 2006u);
    SL_CHECK(scheduler_task_count() >= 1u, 2007u);

    if (g.count > 1u) {
        u32 first = (g.head + SL_RING - g.count) % SL_RING;
        u32 prev = g.ring[first].tick;
        for (u32 i = 1u; i < g.count; i++) {
            u32 idx = (first + i) % SL_RING;
            SL_CHECK(g.ring[idx].tick >= prev, 2008u);
            prev = g.ring[idx].tick;
        }
    } else {
        SL_CHECK(1, 0u);
    }

    if (out) *out = r;
    return (r.checks_failed == 0u) ? 0 : -1;
}

int schedlab_run(u32 mode, u32 rounds, schedlab_run_result_t* out) {
    schedlab_run_result_t r;
    r.rounds = rounds;
    r.mode = mode;
    r.switches_before = g.switches_total;
    r.switches_after = g.switches_total;
    r.fairness_before = g.fairness_score;
    r.fairness_after = g.fairness_score;
    r.risk_before = g.risk_score;
    r.risk_after = g.risk_score;
    r.failures = 0u;

    u32 old_q = scheduler_quantum_level();
    if (rounds < 32u) rounds = 32u;
    if (rounds > 4096u) rounds = 4096u;

    for (u32 i = 0u; i < rounds; i++) {
        if (mode == 0u) {
            scheduler_set_quantum_level(2u);
        } else if (mode == 1u) {
            scheduler_set_quantum_level((i % 2u) ? 1u : 3u);
        } else if (mode == 2u) {
            u32 t = 1u + (i % 3u);
            if (scheduler_set_priority(t, (u8)(5u + (i % 30u))) != 0) r.failures++;
            if (scheduler_set_task_class(t, (sched_class_t)(i % 3u)) != 0) r.failures++;
        } else {
            scheduler_set_quantum_level(1u + (i % 3u));
            u32 t = 1u + (i % 4u);
            if (scheduler_set_realtime(t, (i % 5u) == 0u) != 0) r.failures++;
        }
        schedlab_tick(scheduler_current_task(), 0u, 1u, scheduler_task_count() > 0u ? scheduler_task_count() - 1u : 0u, scheduler_quantum_ticks(), scheduler_quantum_level());
    }

    scheduler_set_quantum_level(old_q);
    recompute_scores();
    r.switches_after = g.switches_total;
    r.fairness_after = g.fairness_score;
    r.risk_after = g.risk_score;
    if (out) *out = r;
    return (r.failures == 0u) ? 0 : -1;
}
