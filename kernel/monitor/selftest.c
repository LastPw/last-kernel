#include "selftest.h"
#include "heap.h"
#include "frame_allocator.h"
#include "serial.h"
#include "file.h"
#include "scheduler.h"
#include "pcoe2.h"
#include "vm.h"
#include "audit.h"
#include "timer.h"
#include "watchdog.h"
#include "paniclog.h"
#include "driver_bus.h"
#include "netstack.h"
#include "nic.h"
#include "vfs.h"
#include "advanced_allocator.h"
#include "ip.h"
#include "memlab.h"
#include "schedlab.h"
#include "iolab.h"
#include "seclab.h"
#include "compatlab.h"
#include "driverlab.h"
#include "fusionlab.h"

#define SELFTEST_MAX_STEP 64u
#define SELFTEST_HEAP_BLOCKS 64u
#define SELFTEST_FRAME_BURST 128u

static selftest_result_t last;

static void reset_result(void) {
    last.total = 0u;
    last.passed = 0u;
    last.failed = 0u;
    last.last_step = 0u;
    last.last_error = 0u;
}

static void pass_step(u32 step) {
    last.total++;
    last.passed++;
    last.last_step = step;
    last.last_error = 0u;
}

static void fail_step(u32 step, u32 err) {
    serial_write("[selftest][FAIL] step=");
    serial_write_dec(step);
    serial_write(" err=");
    serial_write_dec(err);
    serial_putc('\n');
    last.total++;
    last.failed++;
    last.last_step = step;
    last.last_error = err;
    audit_log(AUDIT_EVT_TASK_VIOLATION, step, err);
}

static int check_true(u32 step, int cond, u32 err) {
    if (cond) {
        pass_step(step);
        return 0;
    }
    fail_step(step, err);
    return -1;
}

static int test_heap_integrity_baseline(u32 step) {
    return check_true(step, heap_validate_integrity() == 0, 1001u);
}

static int test_heap_fragmentation_range(u32 step) {
    u32 fr = heap_fragmentation_score();
    return check_true(step, fr <= 100u, 1002u + fr);
}

static int test_heap_stress_pattern(u32 step) {
    void* slots[SELFTEST_HEAP_BLOCKS];
    for (u32 i = 0; i < SELFTEST_HEAP_BLOCKS; i++) slots[i] = 0;

    u32 ok = 0u;
    for (u32 i = 0; i < SELFTEST_HEAP_BLOCKS; i++) {
        u32 sz = 32u + (i % 11u) * 24u;
        slots[i] = heap_alloc(sz);
        if (slots[i]) ok++;
    }

    for (u32 i = 0; i < SELFTEST_HEAP_BLOCKS; i += 2u) {
        if (slots[i]) {
            heap_free(slots[i]);
            slots[i] = 0;
        }
    }

    for (u32 i = 1; i < SELFTEST_HEAP_BLOCKS; i += 2u) {
        if (slots[i]) {
            heap_free(slots[i]);
            slots[i] = 0;
        }
    }

    if (heap_validate_integrity() != 0) {
        fail_step(step, 1003u);
        return -1;
    }
    if (ok < (SELFTEST_HEAP_BLOCKS / 2u)) {
        fail_step(step, 1004u + ok);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_frame_allocator_smoke(u32 step) {
    void* f = frame_alloc();
    if (!f) {
        fail_step(step, 1101u);
        return -1;
    }
    frame_free(f);
    pass_step(step);
    return 0;
}

static int test_frame_allocator_burst(u32 step) {
    void* frames[SELFTEST_FRAME_BURST];
    for (u32 i = 0; i < SELFTEST_FRAME_BURST; i++) frames[i] = 0;

    u32 got = 0u;
    for (u32 i = 0; i < SELFTEST_FRAME_BURST; i++) {
        frames[i] = frame_alloc();
        if (!frames[i]) break;
        got++;
    }

    for (u32 i = 0; i < got; i++) frame_free(frames[i]);

    if (got < 8u) {
        fail_step(step, 1102u + got);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_frame_allocator_order_alloc_free(u32 step) {
    u32 before = frame_used();
    void* b = frame_alloc_order(2u);
    if (!b) {
        fail_step(step, 1105u);
        return -1;
    }
    frame_free_order(b, 2u);
    u32 after = frame_used();
    if (after != before) {
        fail_step(step, 1106u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_frame_metadata_roundtrip(u32 step) {
    void* frame = frame_alloc();
    if (!frame) {
        fail_step(step, 1110u);
        return -1;
    }
    if (frame_meta_get(frame) != 0u ||
        frame_meta_set(frame, FRAME_META_ACCESSED | FRAME_META_DIRTY) != 0 ||
        (frame_meta_get(frame) & (FRAME_META_ACCESSED | FRAME_META_DIRTY)) !=
            (FRAME_META_ACCESSED | FRAME_META_DIRTY) ||
        frame_meta_clear(frame, FRAME_META_DIRTY) != 0 ||
        (frame_meta_get(frame) & FRAME_META_DIRTY) != 0u) {
        frame_free(frame);
        fail_step(step, 1111u);
        return -1;
    }
    frame_free(frame);
    pass_step(step);
    return 0;
}

static int test_file_descriptor_roundtrip(u32 step) {
    const char* path = "/st_fd_selftest.txt";
    const char* text = "fd-ok";
    char out[8];
    u32 fd = 0u;
    u32 dup_fd = 0u;
    u32 pos = 0u;
    vfs_node_info_t info;
    kfile_info_t finfo;

    (void)vfs_delete(path);
    if (kfile_open(path, KFILE_READ | KFILE_WRITE | KFILE_CREATE, &fd) != 0 ||
        kfile_write(fd, text, 5u) != 5 ||
        kfile_seek(fd, 0, KSEEK_SET, &pos) != 0 || pos != 0u ||
        kfile_read(fd, out, 5u) != 5 ||
        out[0] != 'f' || out[1] != 'd' || out[2] != '-' ||
        out[3] != 'o' || out[4] != 'k' ||
        kfile_stat(fd, &info) != 0 || info.size != 5u ||
        kfile_dup(fd, &dup_fd) != 0 || kfile_info(dup_fd, &finfo) != 0 ||
        finfo.used == 0u) {
        (void)kfile_close(fd);
        (void)kfile_close(dup_fd);
        (void)vfs_delete(path);
        fail_step(step, 2510u);
        return -1;
    }
    if (kfile_close(dup_fd) != 0 || kfile_close(fd) != 0 || vfs_delete(path) != 0) {
        fail_step(step, 2511u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_scheduler_quantum_roundtrip(u32 step) {
    u32 q_old = scheduler_quantum_level();
    scheduler_set_quantum_level(1u);
    if (scheduler_quantum_level() != 1u) {
        fail_step(step, 1201u);
        scheduler_set_quantum_level(q_old);
        return -1;
    }
    scheduler_set_quantum_level(3u);
    if (scheduler_quantum_level() != 3u) {
        fail_step(step, 1202u);
        scheduler_set_quantum_level(q_old);
        return -1;
    }
    scheduler_set_quantum_level(q_old);
    pass_step(step);
    return 0;
}

static int test_scheduler_task_count(u32 step) {
    u32 tc = scheduler_task_count();
    if (tc == 0u) {
        fail_step(step, 1203u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_vm_stats_sanity(u32 step) {
    vm_stats_t vs;
    vm_stats(&vs);
    if (vs.total_regions < vs.active_regions) {
        fail_step(step, 1301u);
        return -1;
    }
    if (vs.total_faults < vs.handled_faults) {
        fail_step(step, 1302u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_vm_reclaim_call(u32 step) {
    vm_stats_t before, after;
    vm_stats(&before);
    vm_reclaim_tick();
    vm_stats(&after);
    if (after.committed_pages > before.committed_pages + 4u) {
        fail_step(step, 1303u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_pcoe_status_sanity(u32 step) {
    pcoe2_status_t ps;
    pcoe2_status(&ps);
    if (ps.scheduler_quantum_level < 1u || ps.scheduler_quantum_level > 3u) {
        fail_step(step, 1401u + ps.scheduler_quantum_level);
        return -1;
    }
    if (ps.latency_shadow_level < 1u || ps.latency_shadow_level > 3u) {
        fail_step(step, 1402u + ps.latency_shadow_level);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_pcoe_override_auth_guard(u32 step) {
    int r = pcoe2_apply_override("quantum", 2u);
    if (r != -2 && r != -3) {
        fail_step(step, 1403u + (u32)(r & 0xFF));
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_watchdog_status_sanity(u32 step) {
    watchdog_status_t ws;
    watchdog_status(&ws);
    if (ws.ticks > timer_ticks() + 2u) {
        fail_step(step, 1501u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_watchdog_policy_roundtrip(u32 step) {
    watchdog_policy_t p0, p1;
    watchdog_get_policy(&p0);

    p1 = p0;
    p1.stall_threshold_ticks = 333u;
    p1.vm_oom_spike_threshold = 5u;
    p1.heap_check_period = 48u;
    p1.auto_mitigate = 0u;
    watchdog_set_policy(&p1);

    watchdog_get_policy(&p1);
    if (p1.stall_threshold_ticks != 333u || p1.vm_oom_spike_threshold != 5u || p1.heap_check_period != 48u || p1.auto_mitigate != 0u) {
        fail_step(step, 1502u);
        watchdog_set_policy(&p0);
        return -1;
    }

    watchdog_set_policy(&p0);
    pass_step(step);
    return 0;
}

static int test_paniclog_push_get_clear(u32 step) {
    panic_record_t rec;
    u32 c0 = paniclog_count();
    paniclog_push("selftest-paniclog", 88u, 0xABCDu, 0x12345678u, 0x11112222u,
                  timer_ticks(), heap_corruption_count(), heap_fragmentation_score(),
                  frame_used(), frame_total(), 0u, 0u);

    if (paniclog_count() != c0 + 1u) {
        fail_step(step, 1601u);
        return -1;
    }

    if (paniclog_get(0u, &rec) != 0) {
        fail_step(step, 1602u);
        return -1;
    }
    if (rec.int_no != 88u || rec.eip != 0x12345678u) {
        fail_step(step, 1603u);
        return -1;
    }

    paniclog_clear();
    if (paniclog_count() != 0u) {
        fail_step(step, 1604u);
        return -1;
    }

    pass_step(step);
    return 0;
}

static int test_timer_monotonic(u32 step) {
    u32 a = timer_ticks();
    for (volatile u32 i = 0; i < 10000u; i++) {
        __asm__ __volatile__("nop");
    }
    u32 b = timer_ticks();
    if (b < a) {
        fail_step(step, 1701u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_audit_counter_readable(u32 step) {
    u32 v = audit_violation_count();
    (void)v;
    pass_step(step);
    return 0;
}

static int test_heap_corruption_counter_stable(u32 step) {
    u32 before = heap_corruption_count();
    if (heap_validate_integrity() != 0) {
        fail_step(step, 1801u);
        return -1;
    }
    u32 after = heap_corruption_count();
    if (after < before) {
        fail_step(step, 1802u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_heap_alloc_free_patterns(u32 step) {
    void* a = heap_alloc(96u);
    void* b = heap_alloc(128u);
    void* c = heap_alloc(64u);

    if (!a || !b || !c) {
        if (a) heap_free(a);
        if (b) heap_free(b);
        if (c) heap_free(c);
        fail_step(step, 1803u);
        return -1;
    }

    heap_free(b);
    heap_free(a);
    heap_free(c);

    if (heap_validate_integrity() != 0) {
        fail_step(step, 1804u);
        return -1;
    }

    pass_step(step);
    return 0;
}

static int test_frame_total_used_relation(u32 step) {
    u32 total = frame_total();
    u32 used = frame_used();
    if (used > total) {
        fail_step(step, 1901u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_scheduler_quantum_ticks_relation(u32 step) {
    u32 ql = scheduler_quantum_level();
    u32 qt = scheduler_quantum_ticks();
    if (ql == 1u && qt != 1u) {
        fail_step(step, 1902u);
        return -1;
    }
    if (ql == 2u && qt != 2u) {
        fail_step(step, 1903u);
        return -1;
    }
    if (ql == 3u && qt != 4u) {
        fail_step(step, 1904u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_pcoe_pressure_bounds(u32 step) {
    pcoe2_status_t ps;
    pcoe2_status(&ps);
    if (ps.core_predicted_pressure > 100u) {
        fail_step(step, 2001u);
        return -1;
    }
    if (ps.security_pressure > 100u) {
        fail_step(step, 2002u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_driver_bus_health(u32 step) {
    driver_bus_stats_t dbs;
    driver_bus_stats(&dbs);
    if (dbs.installed == 0u) {
        fail_step(step, 2301u);
        return -1;
    }
    if (dbs.avg_health == 0u || dbs.avg_health > 100u) {
        fail_step(step, 2302u + dbs.avg_health);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_driver_recover_path(u32 step) {
    if (driver_recover("nic") != 0) {
        fail_step(step, 2303u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_netstack_socket_path(u32 step) {
    if (!nic_ready()) {
        pass_step(step);
        return 0;
    }
    int sid = netstack_socket_open(IP_PROTO_UDP, 40000u, 0xC0A80101u, 53u);
    if (sid < 0) {
        fail_step(step, 2401u);
        return -1;
    }
    if (netstack_socket_send((u32)sid, 0, 0) != 0) {
        (void)netstack_socket_close((u32)sid);
        fail_step(step, 2402u);
        return -1;
    }
    if (netstack_socket_close((u32)sid) != 0) {
        fail_step(step, 2403u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_vfs_permissions_and_journal(u32 step) {
    vfs_set_credentials(0u, 0u);
    (void)vfs_delete("/st_selftest.txt");
    if (vfs_create_file("/st_selftest.txt") != 0) {
        fail_step(step, 2501u);
        return -1;
    }
    if (vfs_set_owner("/st_selftest.txt", 1000u, 1000u) != 0) {
        fail_step(step, 2502u);
        return -1;
    }
    if (vfs_set_permissions("/st_selftest.txt", 0600u) != 0) {
        fail_step(step, 2503u);
        return -1;
    }

    vfs_set_credentials(2000u, 2000u);
    if (vfs_write_file("/st_selftest.txt", "x", 1u) == 0) {
        fail_step(step, 2504u);
        return -1;
    }
    vfs_set_credentials(1000u, 1000u);
    if (vfs_write_file("/st_selftest.txt", "ok", 2u) != 0) {
        fail_step(step, 2505u);
        return -1;
    }
    if (vfs_journal_count() == 0u) {
        fail_step(step, 2506u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_allocator_reclaim_hint(u32 step) {
    u32 h = adv_reclaim_step(1024u);
    (void)h;
    pass_step(step);
    return 0;
}

static int test_memlab_validate(u32 step) {
    memlab_validation_t v;
    memlab_tick();
    if (memlab_validate(&v) != 0) {
        fail_step(step, 2701u + v.first_error);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_memlab_run_smoke(u32 step) {
    memlab_run_result_t r;
    int rc = memlab_run(0u, 64u, 256u, &r);
    if (rc != 0 && r.operations == 0u) {
        fail_step(step, 2702u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_schedlab_validate(u32 step) {
    schedlab_validation_t v;
    schedlab_tick(scheduler_current_task(), 0u, 1u, scheduler_task_count(), scheduler_quantum_ticks(), scheduler_quantum_level());
    if (schedlab_validate(&v) != 0) {
        fail_step(step, 2801u + v.first_error);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_schedlab_run_smoke(u32 step) {
    schedlab_run_result_t r;
    int rc = schedlab_run(1u, 64u, &r);
    if (rc != 0 && r.failures > 8u) {
        fail_step(step, 2802u + r.failures);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_iolab_validate(u32 step) {
    iolab_validation_t v;
    iolab_tick();
    if (iolab_validate(&v) != 0) {
        fail_step(step, 2901u + v.first_error);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_iolab_run_smoke(u32 step) {
    iolab_run_result_t r;
    int rc = iolab_run(2u, 64u, 512u, &r);
    if (rc != 0 && r.failures > 8u) {
        fail_step(step, 2902u + r.failures);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_seclab_validate(u32 step) {
    seclab_validation_t v;
    seclab_tick();
    if (seclab_validate(&v) != 0) {
        fail_step(step, 2951u + v.first_error);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_seclab_run_smoke(u32 step) {
    seclab_run_result_t r;
    int rc = seclab_run(1u, 48u, &r);
    if (rc != 0 && r.failures > 8u) {
        fail_step(step, 2952u + r.failures);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_compatlab_validate(u32 step) {
    compatlab_validation_t v;
    compatlab_tick();
    if (compatlab_validate(&v) != 0) {
        fail_step(step, 2961u + v.first_error);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_compatlab_run_smoke(u32 step) {
    compatlab_run_result_t r;
    int rc = compatlab_run(1u, 48u, &r);
    if (rc != 0 && r.failures > 8u) {
        fail_step(step, 2962u + r.failures);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_driverlab_validate(u32 step) {
    driverlab_validation_t v;
    driverlab_tick();
    if (driverlab_validate(&v) != 0) {
        fail_step(step, 2971u + v.first_error);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_driverlab_run_smoke(u32 step) {
    driverlab_run_result_t r;
    int rc = driverlab_run(2u, 48u, &r);
    if (rc != 0 && r.failures > 8u) {
        fail_step(step, 2972u + r.failures);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_fusionlab_validate(u32 step) {
    fusionlab_validation_t v;
    fusionlab_tick();
    if (fusionlab_validate(&v) != 0) {
        fail_step(step, 2981u + v.first_error);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_fusionlab_run_smoke(u32 step) {
    fusionlab_run_result_t r;
    int rc = fusionlab_run(48u, &r);
    if (rc != 0 && r.failures > 8u) {
        fail_step(step, 2982u + r.failures);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_adv_page_alloc_free(u32 step) {
    u32 before_used = frame_used();
    void* p = adv_alloc_pages(3u);
    if (!p) {
        fail_step(step, 2601u);
        return -1;
    }
    adv_free(p);
    if (heap_validate_integrity() != 0) {
        fail_step(step, 2602u);
        return -1;
    }
    if (frame_used() > frame_total()) {
        fail_step(step, 2603u);
        return -1;
    }
    if (before_used > frame_total()) {
        fail_step(step, 2604u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_vm_fault_counters_monotonic(u32 step) {
    vm_stats_t a, b;
    vm_stats(&a);
    vm_reclaim_tick();
    vm_stats(&b);
    if (b.total_faults < a.total_faults) {
        fail_step(step, 2101u);
        return -1;
    }
    pass_step(step);
    return 0;
}

static int test_watchdog_policy_clamp(u32 step) {
    watchdog_policy_t p0, p1;
    watchdog_get_policy(&p0);

    p1 = p0;
    p1.stall_threshold_ticks = 1u;
    p1.vm_oom_spike_threshold = 0u;
    p1.heap_check_period = 0u;
    p1.auto_mitigate = 55u;
    watchdog_set_policy(&p1);
    watchdog_get_policy(&p1);

    if (p1.stall_threshold_ticks < 100u || p1.vm_oom_spike_threshold < 1u || p1.heap_check_period < 8u || p1.auto_mitigate > 1u) {
        fail_step(step, 2201u);
        watchdog_set_policy(&p0);
        return -1;
    }

    watchdog_set_policy(&p0);
    pass_step(step);
    return 0;
}

void selftest_init(void) {
    last.total = 0u;
    last.passed = 0u;
    last.failed = 0u;
    last.last_step = 0u;
    last.last_error = 0u;
    last.last_tick = 0u;
}

int selftest_run_full(selftest_result_t* out) {
    reset_result();

    u32 step = 1u;
    test_heap_integrity_baseline(step++);
    test_heap_fragmentation_range(step++);
    test_heap_stress_pattern(step++);

    test_frame_allocator_smoke(step++);
    test_frame_allocator_burst(step++);
    test_frame_allocator_order_alloc_free(step++);
    test_frame_metadata_roundtrip(step++);
    test_frame_total_used_relation(step++);

    test_scheduler_quantum_roundtrip(step++);
    test_scheduler_task_count(step++);
    test_scheduler_quantum_ticks_relation(step++);

    test_vm_stats_sanity(step++);
    test_vm_reclaim_call(step++);
    test_vm_fault_counters_monotonic(step++);

    test_pcoe_status_sanity(step++);
    test_pcoe_override_auth_guard(step++);
    test_pcoe_pressure_bounds(step++);

    test_watchdog_status_sanity(step++);
    test_watchdog_policy_roundtrip(step++);
    test_watchdog_policy_clamp(step++);

    test_paniclog_push_get_clear(step++);

    test_timer_monotonic(step++);
    test_audit_counter_readable(step++);
    test_heap_corruption_counter_stable(step++);
    test_heap_alloc_free_patterns(step++);
    test_driver_bus_health(step++);
    test_driver_recover_path(step++);
    test_netstack_socket_path(step++);
    test_vfs_permissions_and_journal(step++);
    test_file_descriptor_roundtrip(step++);
    test_allocator_reclaim_hint(step++);
    test_memlab_validate(step++);
    test_memlab_run_smoke(step++);
    test_schedlab_validate(step++);
    test_schedlab_run_smoke(step++);
    test_iolab_validate(step++);
    test_iolab_run_smoke(step++);
    test_seclab_validate(step++);
    test_seclab_run_smoke(step++);
    test_compatlab_validate(step++);
    test_compatlab_run_smoke(step++);
    test_driverlab_validate(step++);
    test_driverlab_run_smoke(step++);
    test_fusionlab_validate(step++);
    test_fusionlab_run_smoke(step++);
    test_adv_page_alloc_free(step++);

    if (step > SELFTEST_MAX_STEP) {
        fail_step(SELFTEST_MAX_STEP, 9999u);
    }

    last.last_tick = timer_ticks();
    if (out) *out = last;
    return (last.failed == 0u) ? 0 : -1;
}

int selftest_run_stress(u32 loops, selftest_result_t* out) {
    reset_result();
    if (loops < 32u) loops = 32u;
    if (loops > 2048u) loops = 2048u;

    for (u32 i = 0; i < loops; i++) {
        u32 step = i + 1u;
        void* p = heap_alloc(64u + (i % 256u));
        if (!p) {
            fail_step(step, 3001u + i);
            continue;
        }
        if ((i % 3u) == 0u) {
            void* f = frame_alloc();
            if (f) frame_free(f);
        }
        if ((i % 5u) == 0u) {
            int sid = netstack_socket_open(IP_PROTO_UDP, (u16)(50000u + (i % 1000u)), 0xC0A80101u, 53u);
            if (sid >= 0) {
                (void)netstack_socket_send((u32)sid, 0, 0);
                (void)netstack_socket_close((u32)sid);
            }
        }
        heap_free(p);
        pass_step(step);
    }
    last.last_tick = timer_ticks();
    if (out) *out = last;
    return (last.failed == 0u) ? 0 : -1;
}

int selftest_run_fuzz(u32 rounds, selftest_result_t* out) {
    reset_result();
    if (rounds < 16u) rounds = 16u;
    if (rounds > 1024u) rounds = 1024u;

    for (u32 i = 0; i < rounds; i++) {
        u32 step = i + 1u;
        char path[32];
        path[0] = '/';
        path[1] = 'f';
        path[2] = 'z';
        path[3] = (char)('0' + (i % 10u));
        path[4] = '.';
        path[5] = 't';
        path[6] = 'x';
        path[7] = 't';
        path[8] = '\0';

        (void)vfs_create_file(path);
        u32 mode = (i & 1u) ? 0644u : 0600u;
        (void)vfs_set_permissions(path, (u16)mode);
        (void)vfs_write_file(path, "fz", 2u);
        if ((i % 4u) == 0u) {
            (void)vfs_set_lock(path, 1u);
        }
        if ((i % 6u) == 0u) {
            (void)vfs_delete(path);
        }
        pass_step(step);
    }
    last.last_tick = timer_ticks();
    if (out) *out = last;
    return (last.failed == 0u) ? 0 : -1;
}

void selftest_status(selftest_result_t* out) {
    if (!out) return;
    *out = last;
}
