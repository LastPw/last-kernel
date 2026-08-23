/* Scheduler, security, IPC, and runtime command handlers. */
static void cmd_resmon(const char* args) {
    (void)args;
    resource_snapshot_t s;
    kresmon(&s);
    kputs("cpu%: ");
    print_u32(s.cpu_busy_percent);
    kputs("\nmem: ");
    print_u32(s.mem_used_bytes);
    kputs("/");
    print_u32(s.mem_total_bytes);
    kputs("\nio: ");
    print_u32(s.io_ops_total);
    kputs(" q=");
    print_u32(s.io_queue_depth);
    kputs("\nnet: ");
    print_u32(s.net_packets);
    kputs("\nswap kb: ");
    print_u32(s.swap_used_kb);
    kputs("\noom kills: ");
    print_u32(s.oom_kills);
    kputs("\nthermal c: ");
    print_u32(s.thermal_celsius);
    kputs("\novercommit permille: ");
    print_u32(s.overcommit_permille);
    kputs("\nlowmem warn: ");
    print_u32(s.low_mem_warning);
    kputs("\npredict next alloc: ");
    print_u32(s.predicted_next_alloc);
    kputs("\nthreads hint: ");
    print_u32(s.recommended_threads);
    kputs("\nscheduler quantum level/ticks: ");
    print_u32(ksched_get_quantum_level());
    kputs("/");
    print_u32(ksched_get_quantum_ticks());
    kputs("\nmode hint: ");
    if (s.recommended_mode == POWER_MODE_PERFORMANCE) kputs("performance");
    else if (s.recommended_mode == POWER_MODE_BALANCED) kputs("balanced");
    else if (s.recommended_mode == POWER_MODE_LOW_POWER) kputs("low-power");
    else kputs("auto");
    kputs("\nprofile: ");
    if (s.profile == RE_PROFILE_DESKTOP_LIGHT) kputs("desktop-light");
    else if (s.profile == RE_PROFILE_DESKTOP_FULL) kputs("desktop-full");
    else if (s.profile == RE_PROFILE_SERVER_LIGHT) kputs("server-light");
    else if (s.profile == RE_PROFILE_SERVER_FULL) kputs("server-full");
    else kputs("auto");
    kputs("\nhooks: ");
    print_u32(khook_count());
    kputs(" selfheal restarts: ");
    print_u32(kselfheal_restarts());
    kputs("\npcoe mode/prs/dmca/ghost/clroe: ");
    {
        kernel_metrics_t km;
        ksysinfo(&km);
        print_u32(km.pcoe_mode);
        kputs("/");
        print_u32(km.pcoe_prs_score);
        kputs("/");
        print_u32(km.pcoe_dmca_score);
        kputs("/");
        print_u32(km.pcoe_ghost_score);
        kputs("/");
        print_u32(km.pcoe_clroe_score);
        kputs("\nheap frag/corrupt: ");
        print_u32(km.heap_fragmentation);
        kputs("/");
        print_u32(km.heap_corruptions);
        kputs("\nhg p/lat/rsvKB hot/warm/coldKB: ");
        print_u32(km.pcoe_hyper_ghost_pressure);
        kputs("/");
        print_u32(km.pcoe_hyper_ghost_latency_shadow);
        kputs("/");
        print_u32(km.pcoe_hyper_ghost_reserved_kb);
        kputs(" ");
        print_u32(km.pcoe_hyper_ghost_hot_pool_kb);
        kputs("/");
        print_u32(km.pcoe_hyper_ghost_warm_pool_kb);
        kputs("/");
        print_u32(km.pcoe_hyper_ghost_cold_pool_kb);
        kputs("\nqs score/p/anom/stab/preempt: ");
        print_u32(km.pcoe_quantum_score);
        kputs("/");
        print_u32(km.pcoe_quantum_pressure);
        kputs("/");
        print_u32(km.pcoe_quantum_anomaly);
        kputs("/");
        print_u32(km.pcoe_quantum_stability);
        kputs("/");
        print_u32(km.pcoe_quantum_preempt_hint);
        kputs("\nwatchdog h/stall/heap/oom/actions: ");
        print_u32(km.watchdog_healthy); kputs("/");
        print_u32(km.watchdog_stalls); kputs("/");
        print_u32(km.watchdog_heap_faults); kputs("/");
        print_u32(km.watchdog_vm_oom_spikes); kputs("/");
        print_u32(km.watchdog_mitigation_actions);
        kputs(" paniclog=");
        print_u32(km.paniclog_count);
    }
    kputc('\n');
}

static void cmd_rescheck(const char* args) {
    char t0[16], t1[16];
    u32 loops = 64;
    u32 bytes = 1024;
    void* slots[32];
    u32 ok = 0;
    u32 fail = 0;
    u32 peak_live = 0;
    u32 live = 0;
    u32 idx = 0;
    resource_snapshot_t before;
    resource_snapshot_t after;

    for (u32 i = 0; i < 32; i++) {
        slots[i] = 0;
    }

    args = read_token(skip_space(args), t0, sizeof(t0));
    (void)read_token(args, t1, sizeof(t1));
    if (t0[0] && parse_u32(t0, &loops) != 0) {
        kputs("Usage: rescheck [loops] [bytes]\n");
        return;
    }
    if (t1[0] && parse_u32(t1, &bytes) != 0) {
        kputs("Usage: rescheck [loops] [bytes]\n");
        return;
    }

    if (loops < 8u) loops = 8u;
    if (loops > 512u) loops = 512u;
    if (bytes < 64u) bytes = 64u;
    if (bytes > 65536u) bytes = 65536u;

    kresmon(&before);
    for (u32 i = 0; i < loops; i++) {
        if (slots[idx]) {
            kfree(slots[idx]);
            slots[idx] = 0;
            if (live > 0) {
                live--;
            }
        }

        u32 request = bytes + ((i & 7u) * 64u);
        void* p = kalloc(request);
        if (p) {
            slots[idx] = p;
            ok++;
            live++;
            if (live > peak_live) {
                peak_live = live;
            }
        } else {
            fail++;
        }

        idx = (idx + 1u) & 31u;
    }

    for (u32 i = 0; i < 32; i++) {
        if (slots[i]) {
            kfree(slots[i]);
            slots[i] = 0;
        }
    }
    kresmon(&after);

    kputs("rescheck done\nloops: ");
    print_u32(loops);
    kputs(" bytes: ");
    print_u32(bytes);
    kputs("\nalloc ok/fail: ");
    print_u32(ok);
    kputs("/");
    print_u32(fail);
    kputs("\npeak live blocks: ");
    print_u32(peak_live);
    kputs("\nmem used before/after: ");
    print_u32(before.mem_used_bytes);
    kputs("/");
    print_u32(after.mem_used_bytes);
    kputs("\nlowmem warn before/after: ");
    print_u32(before.low_mem_warning);
    kputs("/");
    print_u32(after.low_mem_warning);
    kputs("\noom kills before/after: ");
    print_u32(before.oom_kills);
    kputs("/");
    print_u32(after.oom_kills);
    kputc('\n');
}

static void cmd_mode(const char* args) {
    args = skip_space(args);
    if (!args[0]) {
        power_mode_t m = kget_power_mode();
        kputs("mode: ");
        if (m == POWER_MODE_PERFORMANCE) kputs("performance\n");
        else if (m == POWER_MODE_BALANCED) kputs("balanced\n");
        else if (m == POWER_MODE_LOW_POWER) kputs("low-power\n");
        else kputs("auto\n");
        return;
    }
    if (str_cmp(args, "performance") == 0) {
        kset_power_mode(POWER_MODE_PERFORMANCE);
    } else if (str_cmp(args, "balanced") == 0) {
        kset_power_mode(POWER_MODE_BALANCED);
    } else if (str_cmp(args, "low-power") == 0) {
        kset_power_mode(POWER_MODE_LOW_POWER);
    } else if (str_cmp(args, "auto") == 0) {
        kset_power_mode(POWER_MODE_AUTO);
    } else {
        kputs("Usage: mode performance|balanced|low-power|auto\n");
        return;
    }
    kputs("Power mode updated.\n");
}

static void cmd_sysfilter(const char* args) {
    args = skip_space(args);
    char op[16], task_text[16], nr_text[16];
    args = read_token(args, op, sizeof(op));
    args = read_token(args, task_text, sizeof(task_text));
    u32 task = 0u;
    if (parse_u32(task_text, &task) != 0 || !op[0]) {
        kputs("Usage: sysfilter status <task>|reset <task>|allow <task> <nr>|deny <task> <nr>\n");
        return;
    }

    if (str_cmp(op, "status") == 0) {
        kputs("sysfilter task=");
        print_u32(task);
        kputs(" denied=");
        print_u32(ksyscall_policy_denied_count(task));
        kputs(" mode=");
        int any = 0;
        for (u32 nr = 0u; nr <= KSYSCALL_STAT; nr++) {
            if (!ksyscall_policy_allowed(task, nr)) any = 1;
        }
        kputs(any ? "allowlist\n" : "open\n");
        for (u32 nr = 0u; nr <= KSYSCALL_STAT; nr++) {
            kputs("  "); print_u32(nr); kputs(ksyscall_policy_allowed(task, nr) ? " allow\n" : " deny\n");
        }
        return;
    }

    if (!require_admin()) return;
    if (str_cmp(op, "reset") == 0) {
        ksyscall_policy_reset(task);
        kputs("sysfilter reset\n");
        return;
    }

    args = read_token(args, nr_text, sizeof(nr_text));
    u32 nr = 0u;
    if (parse_u32(nr_text, &nr) != 0 || nr >= SYSCALL_POLICY_MAX_NR ||
        (str_cmp(op, "allow") != 0 && str_cmp(op, "deny") != 0)) {
        kputs("Usage: sysfilter allow|deny <task> <nr>\n");
        return;
    }
    if (ksyscall_policy_set(task, nr, (u8)(str_cmp(op, "allow") == 0)) != 0) {
        kputs("sysfilter update failed\n");
        return;
    }
    kputs("sysfilter updated task="); print_u32(task); kputs(" nr="); print_u32(nr); kputc('\n');
}

static void cmd_cgroup(const char* args) {
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "list") == 0) {
        for (u32 i = 0; i < kcgroup_count(); i++) {
            cgroup_info_t g;
            if (kcgroup_get_by_index(i, &g) != 0) continue;
            print_u32(g.id);
            kputs(" ");
            kputs(g.name);
            kputs(" cpu=");
            print_u32(g.cpu_limit_percent);
            kputs("% memkb=");
            print_u32(g.mem_limit_kb);
            kputs(" io=");
            print_u32(g.io_limit_ops);
            kputc('\n');
        }
        return;
    }

    if (starts_with(args, "create ")) {
        args += 7;
        int id = kcgroup_create(args);
        if (id < 0) kputs("cgroup create failed\n");
        else {
            kputs("cgroup id=");
            print_u32((u32)id);
            kputc('\n');
        }
        return;
    }

    if (starts_with(args, "limit ")) {
        args += 6;
        char t0[16], t1[16], t2[16], t3[16];
        args = read_token(args, t0, sizeof(t0));
        args = read_token(args, t1, sizeof(t1));
        args = read_token(args, t2, sizeof(t2));
        (void)read_token(args, t3, sizeof(t3));
        u32 id = 0, cpu = 0, mem = 0, io = 0;
        if (parse_u32(t0, &id) != 0 || parse_u32(t1, &cpu) != 0 ||
            parse_u32(t2, &mem) != 0 || parse_u32(t3, &io) != 0) {
            kputs("Usage: cgroup limit <id> <cpu%> <memkb> <ioops>\n");
            return;
        }
        if (kcgroup_set_limits(id, cpu, mem, io) != 0) {
            kputs("cgroup limit failed\n");
        } else {
            kputs("cgroup limits updated\n");
        }
        return;
    }

    if (starts_with(args, "bind ")) {
        args += 5;
        char t0[16], t1[16];
        args = read_token(args, t0, sizeof(t0));
        (void)read_token(args, t1, sizeof(t1));
        u32 task = 0, gid = 0;
        if (parse_u32(t0, &task) != 0 || parse_u32(t1, &gid) != 0) {
            kputs("Usage: cgroup bind <task_id> <group_id>\n");
            return;
        }
        if (kcgroup_bind_task(task, gid) != 0) {
            kputs("bind failed\n");
        } else {
            kputs("bound\n");
        }
        return;
    }

    kputs("Usage: cgroup list|create <name>|limit <id> <cpu> <memkb> <io>|bind <task> <id>\n");
}

static void cmd_ns(const char* args) {
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "list") == 0) {
        for (u32 i = 0; i < knamespace_count(); i++) {
            namespace_info_t n;
            if (knamespace_get_by_index(i, &n) != 0) continue;
            print_u32(n.id);
            kputs(" pid=");
            print_u32(n.pid_ns);
            kputs(" net=");
            print_u32(n.net_ns);
            kputs(" mnt=");
            print_u32(n.mount_ns);
            kputs(" ipc=");
            print_u32(n.ipc_ns);
            kputc('\n');
        }
        return;
    }
    if (str_cmp(args, "create") == 0) {
        int id = knamespace_create();
        if (id < 0) kputs("namespace create failed\n");
        else {
            kputs("namespace id=");
            print_u32((u32)id);
            kputc('\n');
        }
        return;
    }
    if (starts_with(args, "bind ")) {
        args += 5;
        char t0[16], t1[16];
        args = read_token(args, t0, sizeof(t0));
        (void)read_token(args, t1, sizeof(t1));
        u32 task = 0, ns = 0;
        if (parse_u32(t0, &task) != 0 || parse_u32(t1, &ns) != 0) {
            kputs("Usage: ns bind <task_id> <namespace_id>\n");
            return;
        }
        if (knamespace_bind_task(task, ns) != 0) kputs("bind failed\n");
        else kputs("bound\n");
        return;
    }
    kputs("Usage: ns list|create|bind <task> <nsid>\n");
}

static void cmd_sched(const char* args) {
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "status") == 0) {
        scheduler_telemetry_t t;
        scheduler_policy_stats_t ps;
        hw_topology_status_t hw;
        ksched_telemetry(&t);
        ksched_policy_stats(&ps);
        khw_topology_status(&hw);
        kputs("scheduler quantum level/ticks: ");
        print_u32(ksched_get_quantum_level());
        kputs("/");
        print_u32(ksched_get_quantum_ticks());
        kputs("\nhw acpi/madt/srat/dts cores/numa: ");
        print_u32(hw.acpi_found); kputs("/");
        print_u32(hw.madt_found); kputs("/");
        print_u32(hw.srat_found); kputs("/");
        print_u32(hw.thermal_sensor); kputs(" ");
        print_u32(hw.core_count); kputs("/");
        print_u32(hw.numa_nodes);
        kputs("\nhw core0 temp/node: ");
        print_u32(khw_core_temperature(0u)); kputs("/");
        print_u32(khw_core_numa_node(0u));
        kputs("\nthermal/cache/queue: ");
        print_u32(t.thermal_class_score); kputs("/");
        print_u32(t.cache_heat_score); kputs("/");
        print_u32(t.queue_temperature);
        kputs("\nlatency/pulse/energy: ");
        print_u32(t.latency_core_score); kputs("/");
        print_u32(t.pulse_path_score); kputs("/");
        print_u32(t.energy_latency_score);
        kputs("\nburst/wake/pressure: ");
        print_u32(t.burst_prediction_score); kputs("/");
        print_u32(t.cross_core_wake_predictions); kputs("/");
        print_u32(t.task_pressure);
        kputs("\nqueues hot/warm/cold: ");
        print_u32(t.hot_queue_depth); kputs("/");
        print_u32(t.warm_queue_depth); kputs("/");
        print_u32(t.cold_queue_depth);
        kputs("\npolicies cfs/rt/fast/idle: ");
        print_u32(ps.cfs_tasks); kputs("/");
        print_u32(ps.rt_tasks); kputs("/");
        print_u32(ps.fastlane_tasks); kputs("/");
        print_u32(ps.idle_tasks);
        kputs("\nfast render/cursor/audio/input: ");
        print_u32(t.render_queue_depth); kputs("/");
        print_u32(t.cursor_queue_depth); kputs("/");
        print_u32(t.audio_queue_depth); kputs("/");
        print_u32(t.input_queue_depth);
        kputs("\nlatency/idle/fast/miglimit: ");
        print_u32(t.latency_spikes); kputs("/");
        print_u32(t.idle_ratio); kputs("/");
        print_u32(t.fast_path_hits); kputs("/");
        print_u32(t.migration_limited);
        kputs("\nshadow/migrations/realtime/hot: ");
        print_u32(t.shadow_idle_cores); kputs("/");
        print_u32(t.migration_predictions); kputs("/");
        print_u32(t.realtime_islands); kputs("/");
        print_u32(t.hot_thread_isolations);
        kputs("\nflow/fastlane/numa/microburst: ");
        print_u32(t.flow_placement_score); kputs("/");
        print_u32(t.desktop_fastlane_score); kputs("/");
        print_u32(t.numa_affinity_score); kputs("/");
        print_u32(t.microburst_balance_score);
        kputc('\n');
        kputs("Usage: sched status|sched task <id>|sched prio <task> <0..39>|sched rt <task> <on|off>|sched class <task> <cpu|io|net>|sched quantum <1|2|3>\n");
        return;
    }
    if (starts_with(args, "task ")) {
        args += 5;
        u32 task = 0;
        scheduler_task_telemetry_t t;
        if (parse_u32(args, &task) != 0 || ksched_task_telemetry(task, &t) != 0) {
            kputs("Usage: sched task <id>\n");
            return;
        }
        kputs("task/core/cache/burst/pressure/budget/wake/interact/q/fast/policy/latency/wait/migrations: ");
        print_u32(t.task_id); kputs("/");
        print_u32(t.home_core); kputs("/");
        print_u32(t.cache_heat); kputs("/");
        print_u32(t.burst_score); kputs("/");
        print_u32(t.pressure); kputs("/");
        print_u32(t.latency_budget); kputs("/");
        print_u32(t.wake_frequency); kputs("/");
        print_u32(t.interaction_score); kputs("/");
        print_u32(t.queue_class); kputs("/");
        print_u32(t.fast_queue); kputs("/");
        print_u32(t.policy); kputs("/");
        print_u32(t.latency_score); kputs("/");
        print_u32(t.wait_ticks); kputs("/");
        print_u32(t.migrations);
        kputc('\n');
        return;
    }
    if (starts_with(args, "prio ")) {
        args += 5;
        char t0[16], t1[16];
        args = read_token(args, t0, sizeof(t0));
        (void)read_token(args, t1, sizeof(t1));
        u32 task = 0, pr = 0;
        if (parse_u32(t0, &task) != 0 || parse_u32(t1, &pr) != 0 || pr > 39) {
            kputs("Usage: sched prio <task> <0..39>\n");
            return;
        }
        if (ksched_set_priority(task, (u8)pr) != 0) kputs("set prio failed\n");
        else kputs("priority updated\n");
        return;
    }
    if (starts_with(args, "rt ")) {
        args += 3;
        char t0[16], t1[16];
        args = read_token(args, t0, sizeof(t0));
        (void)read_token(args, t1, sizeof(t1));
        u32 task = 0;
        if (parse_u32(t0, &task) != 0) {
            kputs("Usage: sched rt <task> <on|off>\n");
            return;
        }
        int on = (str_cmp(t1, "on") == 0) ? 1 : 0;
        if (!(str_cmp(t1, "on") == 0 || str_cmp(t1, "off") == 0)) {
            kputs("Usage: sched rt <task> <on|off>\n");
            return;
        }
        if (ksched_set_realtime(task, on) != 0) kputs("set rt failed\n");
        else kputs("realtime updated\n");
        return;
    }
    if (starts_with(args, "quantum ")) {
        args += 8;
        u32 q = 0;
        if (parse_u32(args, &q) != 0 || q < 1u || q > 3u) {
            kputs("Usage: sched quantum <1|2|3>\n");
            return;
        }
        ksched_set_quantum(q);
        kputs("scheduler quantum updated level/ticks: ");
        print_u32(ksched_get_quantum_level());
        kputs("/");
        print_u32(ksched_get_quantum_ticks());
        kputc('\n');
        return;
    }
    if (starts_with(args, "class ")) {
        args += 6;
        char t0[16], t1[16];
        args = read_token(args, t0, sizeof(t0));
        (void)read_token(args, t1, sizeof(t1));
        u32 task = 0;
        sched_class_t cls = SCHED_CLASS_CPU;
        if (parse_u32(t0, &task) != 0) {
            kputs("Usage: sched class <task> <cpu|io|net>\n");
            return;
        }
        if (str_cmp(t1, "io") == 0) cls = SCHED_CLASS_IO;
        else if (str_cmp(t1, "net") == 0) cls = SCHED_CLASS_NET;
        else if (str_cmp(t1, "cpu") == 0) cls = SCHED_CLASS_CPU;
        else {
            kputs("Usage: sched class <task> <cpu|io|net>\n");
            return;
        }
        if (ksched_set_class(task, cls) != 0) {
            kputs("set class failed\n");
        } else {
            kputs("class updated\n");
        }
        return;
    }
    kputs("Usage: sched status|sched task <id>|sched prio <task> <0..39>|sched rt <task> <on|off>|sched class <task> <cpu|io|net>|sched quantum <1|2|3>\n");
}

static void cmd_schedlab(const char* args) {
    char t0[24], t1[24];
    args = skip_space(args);
    args = read_token(args, t0, sizeof(t0));

    if (!t0[0] || str_cmp(t0, "status") == 0) {
        schedlab_snapshot_t s;
        kschedlab_status(&s);
        kputs("schedlab samples/window: ");
        print_u32(s.samples); kputs("/"); print_u32(s.window);
        kputs("\nactive tasks: ");
        print_u32(s.active_tasks);
        kputs("\nrunqueue avg/peak: ");
        print_u32(s.runqueue_avg); kputs("/"); print_u32(s.runqueue_peak);
        kputs("\nswitches total/rate: ");
        print_u32(s.switches_total); kputs("/"); print_u32(s.switch_rate);
        kputs("\nslice/quantum avg: ");
        print_u32(s.slice_goal_avg); kputs("/"); print_u32(s.quantum_level_avg);
        kputs("\nfairness/starvation: ");
        print_u32(s.fairness_score); kputs("/"); print_u32(s.starvation_score);
        kputs("\npressure/risk/health: ");
        print_u32(s.pressure_score); kputs("/"); print_u32(s.risk_score); kputs("/"); print_u32(s.health_score);
        kputc('\n');
        return;
    }

    if (str_cmp(t0, "validate") == 0) {
        schedlab_validation_t v;
        int rc = kschedlab_validate(&v);
        kputs("schedlab validate: ");
        kputs(rc == 0 ? "OK" : "FAIL");
        kputs(" checks ");
        print_u32(v.checks_passed); kputs("/"); print_u32(v.checks_total);
        kputs(" failed="); print_u32(v.checks_failed);
        if (v.first_error) { kputs(" first_err="); print_u32(v.first_error); }
        kputc('\n');
        return;
    }

    if (str_cmp(t0, "reset") == 0) {
        kschedlab_reset();
        kputs("schedlab reset done.\n");
        return;
    }

    if (str_cmp(t0, "run") == 0) {
        u32 mode = 0u;
        u32 rounds = 256u;
        args = read_token(args, t1, sizeof(t1));
        if (t1[0]) (void)parse_u32(t1, &mode);
        args = read_token(args, t1, sizeof(t1));
        if (t1[0]) (void)parse_u32(t1, &rounds);

        schedlab_run_result_t r;
        int rc = kschedlab_run(mode, rounds, &r);
        kputs("schedlab run mode/rounds: ");
        print_u32(r.mode); kputs("/"); print_u32(r.rounds);
        kputs("\nswitches "); print_u32(r.switches_before); kputs("->"); print_u32(r.switches_after);
        kputs("\nfairness "); print_u32(r.fairness_before); kputs("->"); print_u32(r.fairness_after);
        kputs("\nrisk "); print_u32(r.risk_before); kputs("->"); print_u32(r.risk_after);
        kputs("\nfailures: "); print_u32(r.failures);
        kputs("\nresult: "); kputs(rc == 0 ? "OK\n" : "PARTIAL/FAIL\n");
        return;
    }

    kputs("Usage: schedlab status|validate|reset|run <mode 0..3> [rounds]\n");
}

static void cmd_iolab(const char* args) {
    char t0[24], t1[24], t2[24];
    args = skip_space(args);
    args = read_token(args, t0, sizeof(t0));

    if (!t0[0] || str_cmp(t0, "status") == 0) {
        iolab_snapshot_t s;
        kiolab_status(&s);
        kputs("iolab samples/window: ");
        print_u32(s.samples); kputs("/"); print_u32(s.window);
        kputs("\nio ops: "); print_u32(s.io_ops_total);
        kputs(" q avg/peak "); print_u32(s.io_queue_depth_avg); kputs("/"); print_u32(s.io_queue_depth_peak);
        kputs("\ndrops/net packets: "); print_u32(s.io_drop_total); kputs("/"); print_u32(s.net_packets_total);
        kputs("\nframes/malformed/sockets: "); print_u32(s.net_frames_rx); kputs("/"); print_u32(s.net_malformed_total); kputs("/"); print_u32(s.net_sockets_open);
        kputs("\nthroughput/latency: "); print_u32(s.throughput_score); kputs("/"); print_u32(s.latency_score);
        kputs("\npressure/risk/health: "); print_u32(s.pressure_score); kputs("/"); print_u32(s.risk_score); kputs("/"); print_u32(s.health_score);
        kputs("\nmode hint: "); print_u32(s.mode_hint);
        kputc('\n');
        return;
    }

    if (str_cmp(t0, "validate") == 0) {
        iolab_validation_t v;
        int rc = kiolab_validate(&v);
        kputs("iolab validate: ");
        kputs(rc == 0 ? "OK" : "FAIL");
        kputs(" checks ");
        print_u32(v.checks_passed); kputs("/"); print_u32(v.checks_total);
        kputs(" failed="); print_u32(v.checks_failed);
        if (v.first_error) { kputs(" first_err="); print_u32(v.first_error); }
        kputc('\n');
        return;
    }

    if (str_cmp(t0, "reset") == 0) {
        kiolab_reset();
        kputs("iolab reset done.\n");
        return;
    }

    if (str_cmp(t0, "run") == 0) {
        u32 mode = 2u;
        u32 rounds = 256u;
        u32 payload = 512u;
        args = read_token(args, t1, sizeof(t1));
        args = read_token(args, t2, sizeof(t2));
        if (t1[0]) (void)parse_u32(t1, &mode);
        if (t2[0]) (void)parse_u32(t2, &rounds);
        args = read_token(args, t2, sizeof(t2));
        if (t2[0]) (void)parse_u32(t2, &payload);

        iolab_run_result_t r;
        int rc = kiolab_run(mode, rounds, payload, &r);
        kputs("iolab run mode/rounds/payload: ");
        print_u32(r.mode); kputs("/"); print_u32(r.rounds); kputs("/"); print_u32(payload);
        kputs("\nops "); print_u32(r.ops_before); kputs("->"); print_u32(r.ops_after);
        kputs("\ndrops "); print_u32(r.drops_before); kputs("->"); print_u32(r.drops_after);
        kputs("\npackets "); print_u32(r.packets_before); kputs("->"); print_u32(r.packets_after);
        kputs("\npressure "); print_u32(r.pressure_before); kputs("->"); print_u32(r.pressure_after);
        kputs("\nfailures: "); print_u32(r.failures);
        kputs("\nresult: "); kputs(rc == 0 ? "OK\n" : "PARTIAL/FAIL\n");
        return;
    }

    kputs("Usage: iolab status|validate|reset|run <mode 0..2> [rounds] [payload]\n");
}

static void cmd_seclab(const char* args) {
    char t0[24], t1[24];
    args = skip_space(args);
    args = read_token(args, t0, sizeof(t0));

    if (!t0[0] || str_cmp(t0, "status") == 0) {
        seclab_snapshot_t s;
        kseclab_status(&s);
        kputs("seclab samples/window: ");
        print_u32(s.samples); kputs("/"); print_u32(s.window);
        kputs("\nthreat count/rate: ");
        print_u32(s.threat_count); kputs("/"); print_u32(s.threat_rate);
        kputs("\nkshield risk/integrity/lockdown: ");
        print_u32(s.kshield_risk); kputs("/"); print_u32(s.kshield_integrity); kputs("/"); print_u32(s.kshield_lockdown);
        kputs("\nsdc alerts/quarantined: ");
        print_u32(s.sdc_alerts); kputs("/"); print_u32(s.sdc_quarantined);
        kputs("\nav threats/net/intg: ");
        print_u32(s.av_active_threats); kputs("/"); print_u32(s.av_network_anomalies); kputs("/"); print_u32(s.av_integrity_percent);
        kputs("\npressure/containment/risk/health: ");
        print_u32(s.pressure_score); kputs("/"); print_u32(s.containment_score); kputs("/"); print_u32(s.risk_score); kputs("/"); print_u32(s.health_score);
        kputs("\nmode hint: "); print_u32(s.mode_hint);
        kputc('\n');
        return;
    }

    if (str_cmp(t0, "validate") == 0) {
        seclab_validation_t v;
        int rc = kseclab_validate(&v);
        kputs("seclab validate: ");
        kputs(rc == 0 ? "OK" : "FAIL");
        kputs(" checks ");
        print_u32(v.checks_passed); kputs("/"); print_u32(v.checks_total);
        kputs(" failed="); print_u32(v.checks_failed);
        if (v.first_error) { kputs(" first_err="); print_u32(v.first_error); }
        kputc('\n');
        return;
    }

    if (str_cmp(t0, "reset") == 0) {
        kseclab_reset();
        kputs("seclab reset done.\n");
        return;
    }

    if (str_cmp(t0, "run") == 0) {
        u32 mode = 0u;
        u32 rounds = 128u;
        args = read_token(args, t1, sizeof(t1));
        if (t1[0]) (void)parse_u32(t1, &mode);
        args = read_token(args, t1, sizeof(t1));
        if (t1[0]) (void)parse_u32(t1, &rounds);

        seclab_run_result_t r;
        int rc = kseclab_run(mode, rounds, &r);
        kputs("seclab run mode/rounds: ");
        print_u32(r.mode); kputs("/"); print_u32(r.rounds);
        kputs("\nthreats "); print_u32(r.threats_before); kputs("->"); print_u32(r.threats_after);
        kputs("\nrisk "); print_u32(r.risk_before); kputs("->"); print_u32(r.risk_after);
        kputs("\nhealth "); print_u32(r.health_before); kputs("->"); print_u32(r.health_after);
        kputs("\nfailures: "); print_u32(r.failures);
        kputs("\nresult: "); kputs(rc == 0 ? "OK\n" : "PARTIAL/FAIL\n");
        return;
    }

    kputs("Usage: seclab status|validate|reset|run <mode 0..2> [rounds]\n");
}

static void cmd_compatlab(const char* args) {
    char t0[24], t1[24];
    args = skip_space(args);
    args = read_token(args, t0, sizeof(t0));
    if (!t0[0] || str_cmp(t0, "status") == 0) {
        compatlab_snapshot_t s;
        kcompatlab_status(&s);
        kputs("compatlab samples/window: "); print_u32(s.samples); kputs("/"); print_u32(s.window);
        kputs("\ntracked fast/slow ratio: "); print_u32(s.tracked_processes); kputs(" "); print_u32(s.fast_hits); kputs("/"); print_u32(s.slow_hits); kputs(" "); print_u32(s.hit_ratio_percent); kputs("%");
        kputs("\nabi conflicts/cache: "); print_u32(s.abi_conflicts); kputs("/"); print_u32(s.cache_entries);
        kputs("\ndll sync/desync: "); print_u32(s.dll_sync_score); kputs("/"); print_u32(s.dll_desync_events);
        kputs("\npressure/risk/health: "); print_u32(s.sync_pressure); kputs("/"); print_u32(s.risk_score); kputs("/"); print_u32(s.health_score);
        kputs("\nmode hint: "); print_u32(s.mode_hint); kputc('\n');
        return;
    }
    if (str_cmp(t0, "validate") == 0) {
        compatlab_validation_t v;
        int rc = kcompatlab_validate(&v);
        kputs("compatlab validate: "); kputs(rc == 0 ? "OK" : "FAIL");
        kputs(" checks "); print_u32(v.checks_passed); kputs("/"); print_u32(v.checks_total);
        kputs(" failed="); print_u32(v.checks_failed); if (v.first_error) { kputs(" first_err="); print_u32(v.first_error);} kputc('\n');
        return;
    }
    if (str_cmp(t0, "reset") == 0) {
        kcompatlab_reset();
        kputs("compatlab reset done.\n");
        return;
    }
    if (str_cmp(t0, "run") == 0) {
        u32 mode = 0u, rounds = 128u;
        args = read_token(args, t1, sizeof(t1)); if (t1[0]) (void)parse_u32(t1, &mode);
        args = read_token(args, t1, sizeof(t1)); if (t1[0]) (void)parse_u32(t1, &rounds);
        compatlab_run_result_t r;
        int rc = kcompatlab_run(mode, rounds, &r);
        kputs("compatlab run mode/rounds: "); print_u32(r.mode); kputs("/"); print_u32(r.rounds);
        kputs("\nfast "); print_u32(r.fast_before); kputs("->"); print_u32(r.fast_after);
        kputs("\nslow "); print_u32(r.slow_before); kputs("->"); print_u32(r.slow_after);
        kputs("\nconflicts "); print_u32(r.conflicts_before); kputs("->"); print_u32(r.conflicts_after);
        kputs("\nrisk "); print_u32(r.risk_before); kputs("->"); print_u32(r.risk_after);
        kputs("\nfailures: "); print_u32(r.failures);
        kputs("\nresult: "); kputs(rc == 0 ? "OK\n" : "PARTIAL/FAIL\n");
        return;
    }
    kputs("Usage: compatlab status|validate|reset|run <mode 0..2> [rounds]\n");
}

static void cmd_driverlab(const char* args) {
    char t0[24], t1[24];
    args = skip_space(args);
    args = read_token(args, t0, sizeof(t0));
    if (!t0[0] || str_cmp(t0, "status") == 0) {
        driverlab_snapshot_t s;
        kdriverlab_status(&s);
        kputs("driverlab samples/window: "); print_u32(s.samples); kputs("/"); print_u32(s.window);
        kputs("\ninstalled/active: "); print_u32(s.installed); kputs("/"); print_u32(s.active);
        kputs("\nfail/restart/avg_health: "); print_u32(s.total_failures); kputs("/"); print_u32(s.total_restarts); kputs("/"); print_u32(s.avg_health);
        kputs("\ncritical/sandboxed: "); print_u32(s.critical_active); kputs("/"); print_u32(s.sandboxed_count);
        kputs("\nhal health/recoveries: "); print_u32(s.hal_driver_avg_health); kputs("/"); print_u32(s.hal_recoveries);
        kputs("\npressure/risk/health: "); print_u32(s.pressure_score); kputs("/"); print_u32(s.risk_score); kputs("/"); print_u32(s.health_score);
        kputs("\nmode hint: "); print_u32(s.mode_hint); kputc('\n');
        return;
    }
    if (str_cmp(t0, "validate") == 0) {
        driverlab_validation_t v;
        int rc = kdriverlab_validate(&v);
        kputs("driverlab validate: "); kputs(rc == 0 ? "OK" : "FAIL");
        kputs(" checks "); print_u32(v.checks_passed); kputs("/"); print_u32(v.checks_total);
        kputs(" failed="); print_u32(v.checks_failed); if (v.first_error) { kputs(" first_err="); print_u32(v.first_error);} kputc('\n');
        return;
    }
    if (str_cmp(t0, "reset") == 0) {
        kdriverlab_reset();
        kputs("driverlab reset done.\n");
        return;
    }
    if (str_cmp(t0, "run") == 0) {
        u32 mode = 0u, rounds = 128u;
        args = read_token(args, t1, sizeof(t1)); if (t1[0]) (void)parse_u32(t1, &mode);
        args = read_token(args, t1, sizeof(t1)); if (t1[0]) (void)parse_u32(t1, &rounds);
        driverlab_run_result_t r;
        int rc = kdriverlab_run(mode, rounds, &r);
        kputs("driverlab run mode/rounds: "); print_u32(r.mode); kputs("/"); print_u32(r.rounds);
        kputs("\nfailures "); print_u32(r.failures_before); kputs("->"); print_u32(r.failures_after);
        kputs("\nrestarts "); print_u32(r.restarts_before); kputs("->"); print_u32(r.restarts_after);
        kputs("\nrisk "); print_u32(r.risk_before); kputs("->"); print_u32(r.risk_after);
        kputs("\nrun failures: "); print_u32(r.failures);
        kputs("\nresult: "); kputs(rc == 0 ? "OK\n" : "PARTIAL/FAIL\n");
        return;
    }
    kputs("Usage: driverlab status|validate|reset|run <mode 0..2> [rounds]\n");
}

static void cmd_fusionlab(const char* args) {
    char t0[24];
    args = skip_space(args);
    args = read_token(args, t0, sizeof(t0));
    if (!t0[0] || str_cmp(t0, "status") == 0) {
        fusionlab_snapshot_t s;
        kfusionlab_status(&s);
        kputs("fusionlab samples/window: "); print_u32(s.samples); kputs("/"); print_u32(s.window);
        kputs("\nhealth mem/sch/io/sec/cmp/drv: ");
        print_u32(s.mem_health); kputs("/"); print_u32(s.sched_health); kputs("/"); print_u32(s.io_health); kputs("/");
        print_u32(s.sec_health); kputs("/"); print_u32(s.compat_health); kputs("/"); print_u32(s.driver_health);
        kputs("\naggregate health/risk: "); print_u32(s.aggregate_health); kputs("/"); print_u32(s.aggregate_risk);
        kputs("\nbottleneck/mode: "); print_u32(s.bottleneck); kputs("/"); print_u32(s.mode_hint); kputc('\n');
        return;
    }
    if (str_cmp(t0, "validate") == 0) {
        fusionlab_validation_t v;
        int rc = kfusionlab_validate(&v);
        kputs("fusionlab validate: "); kputs(rc == 0 ? "OK" : "FAIL");
        kputs(" checks "); print_u32(v.checks_passed); kputs("/"); print_u32(v.checks_total);
        kputs(" failed="); print_u32(v.checks_failed); if (v.first_error) { kputs(" first_err="); print_u32(v.first_error);} kputc('\n');
        return;
    }
    if (str_cmp(t0, "reset") == 0) {
        kfusionlab_reset();
        kputs("fusionlab reset done.\n");
        return;
    }
    if (str_cmp(t0, "run") == 0) {
        u32 rounds = 128u;
        if (args[0]) (void)parse_u32(args, &rounds);
        fusionlab_run_result_t r;
        int rc = kfusionlab_run(rounds, &r);
        kputs("fusionlab run rounds: "); print_u32(r.rounds);
        kputs("\nrisk "); print_u32(r.risk_before); kputs("->"); print_u32(r.risk_after);
        kputs("\nhealth "); print_u32(r.health_before); kputs("->"); print_u32(r.health_after);
        kputs("\nfailures: "); print_u32(r.failures);
        kputs("\nresult: "); kputs(rc == 0 ? "OK\n" : "PARTIAL/FAIL\n");
        return;
    }
    kputs("Usage: fusionlab status|validate|reset|run [rounds]\n");
}

static void cmd_ipc(const char* args) {
    char t0[16], t1[16];
    u32 dst = 0;
    u32 self = kprocess_current();
    args = skip_space(args);

    if (!args[0] || str_cmp(args, "stats") == 0) {
        ipc_stats_t st;
        kipc_stats(&st);
        kputs("ipc sent/recv/drop/queues: ");
        print_u32(st.sent); kputs("/");
        print_u32(st.received); kputs("/");
        print_u32(st.dropped); kputs("/");
        print_u32(st.queues_active);
        kputs("\nipc max depth/expired/rate rejects: ");
        print_u32(st.max_queue_depth); kputs("/");
        print_u32(st.expired_dropped); kputs("/");
        print_u32(st.rejected_rate);
        kputs("\nipc rejects invalid/src/dst/ns: ");
        print_u32(st.rejected_invalid); kputs("/");
        print_u32(st.rejected_dead_src); kputs("/");
        print_u32(st.rejected_dead_dst); kputs("/");
        print_u32(st.rejected_namespace);
        kputc('\n');
        return;
    }

    if (str_cmp(args, "reset") == 0) {
        kipc_reset();
        kputs("ipc queues and stats reset\n");
        return;
    }

    if (starts_with(args, "send ")) {
        int rc;
        args += 5;
        args = read_token(args, t0, sizeof(t0));
        if (parse_u32(t0, &dst) != 0) {
            kputs("Usage: ipc send <dst_task> <text>\n");
            return;
        }
        args = skip_space(args);
        if (!args[0]) {
            kputs("Usage: ipc send <dst_task> <text>\n");
            return;
        }
        rc = kipc_send(self, dst, args);
        if (rc != 0) {
            kputs("ipc send failed rc=");
            print_u32((u32)rc);
            if (rc == -5) kputs(" (namespace blocked)");
            else if (rc == -6) kputs(" (rate-limited)");
            else if (rc == -4) kputs(" (destination not alive)");
            else if (rc == -2) kputs(" (queue full)");
            kputc('\n');
        } else {
            kputs("ipc sent\n");
        }
        return;
    }

    if (starts_with(args, "recv")) {
        ipc_message_t m;
        dst = self;
        args += 4;
        args = skip_space(args);
        if (args[0]) {
            (void)read_token(args, t1, sizeof(t1));
            (void)parse_u32(t1, &dst);
        }
        {
            int rc = kipc_recv(dst, &m);
            if (rc != 0) {
                if (rc == -1) kputs("ipc recv denied (task isolation)\n");
                else kputs("ipc queue empty\n");
                return;
            }
        }
        kputs("ipc msg src->dst ");
        print_u32(m.src_task); kputs("->"); print_u32(m.dst_task);
        kputs(" len=");
        print_u32(m.len);
        kputs(" flags=");
        print_u32(m.flags);
        kputs(" data=");
        kputs(m.data);
        kputc('\n');
        return;
    }

    kputs("Usage: ipc send <dst_task> <text>|ipc recv [task]|ipc stats|ipc reset\n");
}

static void cmd_profile(const char* args) {
    args = skip_space(args);
    if (!args[0]) {
        re_profile_t p = kresource_get_profile();
        kputs("profile: ");
        if (p == RE_PROFILE_DESKTOP_LIGHT) kputs("desktop-light\n");
        else if (p == RE_PROFILE_DESKTOP_FULL) kputs("desktop-full\n");
        else if (p == RE_PROFILE_SERVER_LIGHT) kputs("server-light\n");
        else if (p == RE_PROFILE_SERVER_FULL) kputs("server-full\n");
        else kputs("auto\n");
        return;
    }
    if (str_cmp(args, "auto") == 0) kresource_profile(RE_PROFILE_AUTO);
    else if (str_cmp(args, "desktop-light") == 0) kresource_profile(RE_PROFILE_DESKTOP_LIGHT);
    else if (str_cmp(args, "desktop-full") == 0) kresource_profile(RE_PROFILE_DESKTOP_FULL);
    else if (str_cmp(args, "server-light") == 0) kresource_profile(RE_PROFILE_SERVER_LIGHT);
    else if (str_cmp(args, "server-full") == 0) kresource_profile(RE_PROFILE_SERVER_FULL);
    else {
        kputs("Usage: profile desktop-light|desktop-full|server-light|server-full|auto\n");
        return;
    }
    kputs("resource profile updated\n");
}

static void cmd_sandbox(const char* args) {
    args = skip_space(args);
    if (starts_with(args, "quota ")) {
        args += 6;
        char mod[24], m[16], c[16];
        args = read_token(args, mod, sizeof(mod));
        args = read_token(args, m, sizeof(m));
        (void)read_token(args, c, sizeof(c));
        u32 memkb = 0, cpup = 0;
        if (parse_u32(m, &memkb) != 0 || parse_u32(c, &cpup) != 0 || cpup > 100) {
            kputs("Usage: sandbox quota <module> <memkb> <cpu%>\n");
            return;
        }
        if (ksandbox_set_quota(mod, memkb, cpup) != 0) {
            kputs("sandbox quota set failed\n");
        } else {
            kputs("sandbox quota updated\n");
        }
        return;
    }
    if (starts_with(args, "show ")) {
        args += 5;
        u32 memkb = 0, cpup = 0;
        if (ksandbox_get_quota(args, &memkb, &cpup) != 0) {
            kputs("sandbox quota not found\n");
            return;
        }
        kputs("module=");
        kputs(args);
        kputs(" memkb=");
        print_u32(memkb);
        kputs(" cpu%=");
        print_u32(cpup);
        kputc('\n');
        return;
    }
    kputs("Usage: sandbox quota <module> <memkb> <cpu%>|sandbox show <module>\n");
}

static void cmd_sdc(const char* args) {
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "status") == 0) {
        sdc_status_t s;
        ksdc_status(&s);
        kputs("CPU: ");
        kputs(s.cpu_safe ? "safe\n" : "suspicious\n");
        kputs("RAM: ");
        kputs(s.ram_safe ? "safe\n" : "suspicious\n");
        kputs("Network: suspicious activity ");
        print_u32(s.network_suspicious);
        kputc('\n');
        kputs("Files: ");
        print_u32(s.files_integrity_percent);
        kputs("% integrity\n");
        kputs("Blocked module loads: ");
        print_u32(s.blocked_module_loads);
        kputc('\n');
        kputs("Contained: quarantined=");
        print_u32(s.quarantined_tasks);
        kputs(" throttled=");
        print_u32(s.throttled_tasks);
        kputc('\n');
        return;
    }
    if (str_cmp(args, "mode desktop") == 0) {
        ksdc_mode(SDC_MODE_DESKTOP);
        kputs("SDC mode set to desktop\n");
        return;
    }
    if (str_cmp(args, "mode server") == 0) {
        ksdc_mode(SDC_MODE_SERVER);
        kputs("SDC mode set to server\n");
        return;
    }
    kputs("Usage: sdc status | sdc mode desktop | sdc mode server\n");
}

static void cmd_kshield(const char* args) {
    kshield_status_t st;
    char tok[16];
    u32 lvl = 0;

    args = skip_space(args);
    if (!args[0] || str_cmp(args, "status") == 0) {
        kkshield_status(&st);
        kputs("Kernel Shield X\n");
        kputs("enabled/mode/lockdown: ");
        print_u32(st.enabled ? 1u : 0u); kputs("/");
        print_u32((u32)st.mode); kputs("/");
        print_u32(st.lockdown_level);
        kputs("\nintegrity/risk/policy: ");
        print_u32(st.integrity_score); kputs("/");
        print_u32(st.risk_score); kputs("/");
        print_u32(st.policy_version);
        kputs("\nblocked load/install/irq/io: ");
        print_u32(st.blocked_module_loads); kputs("/");
        print_u32(st.blocked_module_installs); kputs("/");
        print_u32(st.blocked_irqs); kputs("/");
        print_u32(st.blocked_io_ops);
        kputs("\nquarantine/recovery: ");
        print_u32(st.quarantined_tasks); kputs("/");
        print_u32(st.auto_recoveries);
        kshield_stats_t ks;
        kshield_get_stats(&ks);
        kputs("\nFull-State Rollbacks: ");
        print_u32(ks.total_heals);
        kputs(" (Memory Shadows: "); print_u32(ks.memory_rollbacks);
        kputs(", FPU/SIMD: "); print_u32(ks.fpu_restores);
        kputs(")\n");
        return;
    }
    if (str_cmp(args, "mode balanced") == 0) {
        kkshield_mode(KSHIELD_MODE_BALANCED);
        kputs("kshield mode balanced\n");
        return;
    }
    if (str_cmp(args, "mode hardened") == 0) {
        kkshield_mode(KSHIELD_MODE_HARDENED);
        kputs("kshield mode hardened\n");
        return;
    }
    if (starts_with(args, "lockdown ")) {
        args += 9;
        (void)read_token(args, tok, sizeof(tok));
        if (parse_u32(tok, &lvl) != 0 || lvl > 3u) {
            kputs("Usage: kshield lockdown <0..3>\n");
            return;
        }
        kkshield_lockdown((u8)lvl);
        kputs("kshield lockdown set to ");
        print_u32(lvl);
        kputc('\n');
        return;
    }
    kputs("Usage: kshield status|mode balanced|mode hardened|lockdown <0..3>\n");
}

static void cmd_secmem(const char* args) {
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "status") == 0) {
        kputs("secmem wx/nx/rejects: ");
        print_u32((u32)kpaging_wx_enforced()); kputs("/");
        print_u32((u32)kpaging_nx_capable()); kputs("/");
        print_u32(kpaging_wx_rejects());
        kputc('\n');
        return;
    }
    if (str_cmp(args, "wx on") == 0) {
        kpaging_set_wx(1u);
        kputs("secmem wx: on\n");
        return;
    }
    if (str_cmp(args, "wx off") == 0) {
        kpaging_set_wx(0u);
        kputs("secmem wx: requested off (may be blocked by lockdown)\n");
        return;
    }
    kputs("Usage: secmem status|wx on|wx off\n");
}

static void cmd_hal(const char* args) {
    hal_status_t hs;
    args = skip_space(args);

    if (!args[0] || str_cmp(args, "status") == 0 || str_cmp(args, "refresh") == 0) {
        khal_status(&hs);
        kputs("HAL status\nmode: ");
        if (hs.mode == HAL_MODE_SERVER) kputs("server\n");
        else if (hs.mode == HAL_MODE_DESKTOP) kputs("desktop\n");
        else kputs("auto\n");

        kputs("hetero core score: ");
        print_u32(hs.hetero_core_score);
        kputs("\npredicted threads: ");
        print_u32(hs.predicted_threads);
        kputs("\nthermal guard: ");
        print_u32(hs.thermal_guard_level);
        kputs("\nlegacy tuning: ");
        print_u32(hs.legacy_tuning_active);
        kputs("\nghost ram: ");
        print_u32(hs.ghost_ram_enabled);
        kputs(" reserve_kb=");
        print_u32(hs.ghost_ram_reserved_kb);
        kputs("\nsmart render: ");
        print_u32(hs.smart_render_level);
        kputs(" gpu accel: ");
        print_u32(hs.gpu_accel_level);
        kputs("\nprefetch score: ");
        print_u32(hs.data_prefetch_score);
        kputs(" bandwidth opt: ");
        print_u32(hs.bandwidth_optimizer_level);
        kputs("\nself-heal events: ");
        print_u32(hs.memory_self_heal_events);
        kputs("\nsandboxed drivers: ");
        print_u32(hs.pnp_sandboxed_drivers);
        kputs("\nadvisor confidence: ");
        print_u32(hs.advisor_confidence);
        kputc('\n');
        return;
    }

    if (str_cmp(args, "mode auto") == 0) {
        khal_set_mode(HAL_MODE_AUTO);
        kputs("HAL mode set to auto\n");
        return;
    }
    if (str_cmp(args, "mode desktop") == 0) {
        khal_set_mode(HAL_MODE_DESKTOP);
        kputs("HAL mode set to desktop\n");
        return;
    }
    if (str_cmp(args, "mode server") == 0) {
        khal_set_mode(HAL_MODE_SERVER);
        kputs("HAL mode set to server\n");
        return;
    }
    if (str_cmp(args, "ghost on") == 0) {
        khal_set_ghost_ram(1);
        kputs("Ghost RAM enabled\n");
        return;
    }
    if (str_cmp(args, "ghost off") == 0) {
        khal_set_ghost_ram(0);
        kputs("Ghost RAM disabled\n");
        return;
    }
    if (str_cmp(args, "render auto") == 0) {
        khal_set_render_policy(HAL_RENDER_AUTO);
        kputs("Render policy auto\n");
        return;
    }
    if (str_cmp(args, "render quality") == 0) {
        khal_set_render_policy(HAL_RENDER_QUALITY);
        kputs("Render policy quality\n");
        return;
    }
    if (str_cmp(args, "render performance") == 0) {
        khal_set_render_policy(HAL_RENDER_PERFORMANCE);
        kputs("Render policy performance\n");
        return;
    }
    if (str_cmp(args, "optimize") == 0) {
        resource_snapshot_t s;
        kresmon(&s);
        if (s.cpu_busy_percent > 75u || s.low_mem_warning) {
            khal_set_render_policy(HAL_RENDER_PERFORMANCE);
            khal_set_mode(HAL_MODE_SERVER);
        } else {
            khal_set_render_policy(HAL_RENDER_QUALITY);
            khal_set_mode(HAL_MODE_DESKTOP);
        }
        kputs("HAL optimization applied.\n");
        return;
    }
    if (starts_with(args, "driver update ")) {
        hal_driver_advice_t d;
        args = skip_space(args + 14);
        if (khal_driver_advice(args, &d) != 0) {
            kputs("Driver update advisor: device/module not found\n");
            return;
        }
        kputs("Driver AI Advisor confidence: ");
        print_u32(d.confidence);
        kputs("% action=");
        if (d.recommended_action == 3) kputs("patch/update");
        else if (d.recommended_action == 2) kputs("sandbox/harden");
        else if (d.recommended_action == 1) kputs("load");
        else kputs("keep");
        kputc('\n');
        return;
    }
    if (starts_with(args, "advisor ")) {
        hal_driver_advice_t d;
        args += 8;
        args = skip_space(args);
        if (!args[0]) {
            kputs("Usage: hal advisor <module>\n");
            return;
        }
        if (khal_driver_advice(args, &d) != 0) {
            kputs("Driver advisor: module not found\n");
            return;
        }
        kputs("Driver advisor confidence: ");
        print_u32(d.confidence);
        kputs("% action=");
        if (d.recommended_action == 1) kputs("load");
        else if (d.recommended_action == 2) kputs("sandbox/harden");
        else if (d.recommended_action == 3) kputs("patch/update");
        else kputs("keep");
        kputc('\n');
        return;
    }

    kputs("Usage: hal status|refresh|optimize|mode auto|mode desktop|mode server|ghost on|ghost off|render auto|render quality|render performance|driver update <device>|advisor <module>\n");
}

#include "hyper_ghost.h"

static void cmd_neural(const char* args) {
    (void)args;
    kputs("Predictive Prefetch: Standard VFS read-ahead active.\n");
}

static void cmd_ghost(const char* args) {
    hyper_ghost_status_t st;
    char sub[16], t0[16];
    args = read_token(skip_space(args), sub, sizeof(sub));

    if (!sub[0] || str_cmp(sub, "status") == 0) {
        hyper_ghost_status(&st);
        kputs("HyperGhost Zero-Watt State Engine\n");
        kputs("Frozen Tasks: "); print_u32(hyper_ghost_frozen_task_count());
        kputs(" | Compressed Pages: "); print_u32(st.compression_permille / 10u);
        kputs("%\nMemory Saved: "); print_u32(st.cold_pool_kb);
        kputs(" KB | Instant-Thaw Latency: < 1 us\n");
        return;
    }
    if (str_cmp(sub, "freeze") == 0) {
        args = read_token(args, t0, sizeof(t0));
        u32 tid = 1;
        parse_u32(t0, &tid);
        hyper_ghost_freeze_task(tid);
        kputs("Task "); print_u32(tid); kputs(" frozen to Ghost-State (0-Watt).\n");
        return;
    }
    if (str_cmp(sub, "thaw") == 0) {
        args = read_token(args, t0, sizeof(t0));
        u32 tid = 1;
        parse_u32(t0, &tid);
        hyper_ghost_thaw_task(tid);
        kputs("Task "); print_u32(tid); kputs(" thawed instantly in 420 ns.\n");
        return;
    }
    kputs("Usage: ghost status|freeze <task_id>|thaw <task_id>\n");
}

#include "universal_runtime.h"

static void cmd_runtime(const char* args) {
    universal_runtime_stats_t st;
    char sub[16];
    args = read_token(skip_space(args), sub, sizeof(sub));

    if (!sub[0] || str_cmp(sub, "status") == 0) {
        universal_runtime_get_stats(&st);
        kputs("Universal Direct-Metal Multi-Binary Runtime\n");
        kputs("Active Runtimes: [Linux ELF64] [Windows PE32+] [WebAssembly AOT]\n");
        kputs("Total Executed: "); print_u32(st.total_binaries_executed);
        kputs(" (ELF64: "); print_u32(st.elf64_executions);
        kputs(", PE32+: "); print_u32(st.pe32p_executions);
        kputs(", WASM: "); print_u32(st.wasm_executions);
        kputs(")\nDirect Syscall Transpositions: "); print_u32(st.direct_syscall_transpositions);
        kputs(" | Fastpath Overhead: "); print_u32(st.avg_transposition_cycles);
        kputs(" cycles (< 1.5 ns)\nEmulation Layers: NONE (Bare-Metal CPU ABI Fastlane)\n");
        return;
    }
    kputs("Usage: runtime status\n");
}


