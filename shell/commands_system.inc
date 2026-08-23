/* System, process, module, and security command handlers. */
static void cmd_list(const char* args) {
    (void)args;
    commands_list();
}

static void cmd_help(const char* args) {
    args = skip_space(args);
    if (!args[0]) {
        commands_list();
        return;
    }

    const cmd_def_t* c = find_command(args);
    if (!c || !module_available(c->module)) {
        kputs("No inline help for command.\n");
        return;
    }
    kputs(c->name);
    kputs(" - ");
    kputs(c->desc);
    kputc('\n');
    kputs("Usage: ");
    kputs(c->usage);
    kputc('\n');
}

static void cmd_past(const char* args) {
    (void)args;
    history_print();
}

static void cmd_clear(const char* args) {
    (void)args;
    kclear();
}

static void cmd_ticks(const char* args) {
    (void)args;
    kputs("ticks: ");
    print_u32(kticks());
    kputc('\n');
}

static void cmd_reboot(const char* args) {
    (void)args;
    kputs("Rebooting system...\n");
    kreboot();
}

/* ========== Full Disk Encryption ========== */

static void cmd_fde_format(const char* args) {
    u32 start_lba, total_lba;
    char password[FDE_MAX_PASSWORD];
    int rc;

    args = skip_space(args);
    if (!args[0]) {
        kputs("Usage: fde-format <start_lba> <total_lba>\n");
        kputs("You will be prompted for a passphrase.\n");
        kputs("WARNING: This ERASES all data on the target partition!\n");
        return;
    }
    if (parse_u32(args, &start_lba) != 0) { kputs("Invalid start_lba\n"); return; }
    while (*args && *args != ' ') args++; args = skip_space(args);
    if (parse_u32(args, &total_lba) != 0) { kputs("Invalid total_lba\n"); return; }

    kputs("Enter passphrase: ");
    {
        u32 i = 0;
        char c;
        while (i < FDE_MAX_PASSWORD - 1) {
            c = (char)kgetc();
            if (c == '\r' || c == '\n') break;
            if (c == '\b' || c == 127) { if (i > 0) { i--; kputs("\b \b"); } continue; }
            password[i++] = c;
            kputc('*');
        }
        password[i] = '\0';
        kputc('\n');
    }

    rc = fde_format(0, start_lba, total_lba, password);
    memset(password, 0, FDE_MAX_PASSWORD);

    if (rc == FDE_OK) kputs("[OK] FDE format complete. Partition is now encrypted.\n");
    else if (rc == FDE_ERR_ALREADY) kputs("[ERR] Partition already has FDE header.\n");
    else if (rc == FDE_ERR_IO) kputs("[ERR] I/O error during format.\n");
    else kputs("[ERR] Format failed.\n");
}

static void cmd_fde_unlock(const char* args) {
    u32 start_lba;
    char password[FDE_MAX_PASSWORD];
    int rc;

    args = skip_space(args);
    if (!args[0]) {
        kputs("Usage: fde-unlock <start_lba>\n");
        kputs("Enter passphrase to unlock an FDE-encrypted partition.\n");
        kputs("Tip: use 'disk list' to find partition start_lba.\n");
        return;
    }
    if (parse_u32(args, &start_lba) != 0) { kputs("Invalid start_lba\n"); return; }

    if (fde_is_active()) {
        kputs("[WARN] FDE is already active! Use 'fde-lock' first.\n");
        return;
    }

    kputs("Enter passphrase: ");
    {
        u32 i = 0;
        char c;
        while (i < FDE_MAX_PASSWORD - 1) {
            c = (char)kgetc();
            if (c == '\r' || c == '\n') break;
            if (c == '\b' || c == 127) { if (i > 0) { i--; kputs("\b \b"); } continue; }
            password[i++] = c;
            kputc('*');
        }
        password[i] = '\0';
        kputc('\n');
    }

    rc = fde_unlock(0, start_lba, password);
    memset(password, 0, FDE_MAX_PASSWORD);

    if (rc == FDE_OK) {
        kputs("[OK] FDE unlocked. Data is now transparently decrypted.\n");
        kputs("All subsequent disk reads/writes will be decrypted/encrypted.\n");
    } else if (rc == FDE_ERR_MAGIC) {
        kputs("[ERR] No FDE header found at this LBA.\n");
    } else if (rc == FDE_ERR_AUTH) {
        kputs("[ERR] Wrong passphrase!\n");
    } else if (rc == FDE_ERR_IO) {
        kputs("[ERR] I/O error.\n");
    } else {
        kputs("[ERR] Unlock failed.\n");
    }
}

static void cmd_fde_lock(const char* args) {
    (void)args;
    if (!fde_is_active()) {
        kputs("FDE is not active.\n");
        return;
    }
    fde_lock();
    kputs("[OK] FDE locked. Encryption keys cleared from memory.\n");
}

static void cmd_fde_status(const char* args) {
    (void)args;
    if (fde_is_active()) {
        u32 total = 0;
        fde_status(&total);
        kputs("FDE Status: ACTIVE\n");
        kputs("Encrypted sectors: ");
        print_u32(total);
        kputc('\n');
    } else {
        kputs("FDE Status: INACTIVE (no encryption active)\n");
    }
}

static void cmd_shutdown(const char* args) {
    (void)args;
    kputs("Shutting down system...\n");
    kshutdown();
}

static void cmd_status(const char* args) {
    (void)args;
    kernel_metrics_t m;
    ksysinfo(&m);
    kputs("Protected Mode active\nInterrupts enabled\nKernel tick counter: ");
    print_u32(m.ticks);
    kputs("\nDynamic timer Hz: ");
    print_u32(m.timer_hz);
    kputs("\nScheduler quantum level/ticks: ");
    print_u32(m.scheduler_quantum_level);
    kputs("/");
    print_u32(m.scheduler_quantum_ticks);
    kputs("\nKernel core mode/perf/risk: ");
    print_u32(m.ukernel_mode);
    kputs("/");
    print_u32(m.ukernel_perf_score);
    kputs("/");
    print_u32(m.ukernel_risk_score);
    kputs("\nPCOE prs/dmca/clroe: ");
    print_u32(m.pcoe_prs_score);
    kputs("/");
    print_u32(m.pcoe_dmca_score);
    kputs("/");
    print_u32(m.pcoe_clroe_score);
    kputs("\nPCOE qpredict/security: ");
    print_u32(m.pcoe_quantum_predictive_score);
    kputs("/");
    print_u32(m.pcoe_security_pressure);
    kputs("\nHG pressure/reservedKB/shadow: ");
    print_u32(m.pcoe_hyper_ghost_pressure);
    kputs("/");
    print_u32(m.pcoe_hyper_ghost_reserved_kb);
    kputs("/");
    print_u32(m.pcoe_hyper_ghost_shadow_pages);
    kputs("\nQS score/pressure/preempt: ");
    print_u32(m.pcoe_quantum_score);
    kputs("/");
    print_u32(m.pcoe_quantum_pressure);
    kputs("/");
    print_u32(m.pcoe_quantum_preempt_hint);
    kputs("\nVM active/faults/handled/guard/oom: ");
    print_u32(m.vm_active_regions);
    kputs("/");
    print_u32(m.vm_total_faults);
    kputs("/");
    print_u32(m.vm_handled_faults);
    kputs("/");
    print_u32(m.vm_guard_faults);
    kputs("/");
    print_u32(m.vm_oom_faults);
    kputs("\nHeap used/peak/corrupt/frag%: ");
    print_u32(m.heap_used);
    kputs("/");
    print_u32(m.heap_peak);
    kputs("/");
    print_u32(m.heap_corruptions);
    kputs("/");
    print_u32(m.heap_fragmentation);
    kputs("\nWatchdog healthy/stalls/heap/oom/actions: ");
    print_u32(m.watchdog_healthy);
    kputs("/");
    print_u32(m.watchdog_stalls);
    kputs("/");
    print_u32(m.watchdog_heap_faults);
    kputs("/");
    print_u32(m.watchdog_vm_oom_spikes);
    kputs("/");
    print_u32(m.watchdog_mitigation_actions);
    kputs(" paniclog: ");
    print_u32(m.paniclog_count);
    kputc('\n');
}

static void cmd_alloc(const char* args) {
    u32 size = 0;
    if (parse_u32(args, &size) != 0 || size == 0) {
        kputs("Usage: alloc <bytes>\n");
        return;
    }
    last_alloc = kalloc(size);
    if (!last_alloc) {
        kputs("Allocation failed\n");
        return;
    }
    kputs("Allocated\n");
}

static void cmd_free(const char* args) {
    (void)args;
    if (!last_alloc) {
        kputs("No allocation to free\n");
        return;
    }
    kfree(last_alloc);
    last_alloc = 0;
    kputs("Freed\n");
}

static void cmd_panic(const char* args) {
    if (args && args[0]) {
        panic(args);
    } else {
        panic("manual panic");
    }
}

static void cmd_echo(const char* args) {
    kputs(args);
    kputc('\n');
}

static void cmd_sysinfo(const char* args) {
    (void)args;
    kernel_metrics_t m;
    ksysinfo(&m);
    shell_sysinfo_print();
    kputs("io ops: ");
    print_u32(m.io_ops_total);
    kputs("\nnet packets: ");
    print_u32(m.net_packets);
    kputs("\nswap used kb: ");
    print_u32(m.swap_used_kb);
    kputs("\noom kills: ");
    print_u32(m.oom_kills);
    {
        memrt_snapshot_t rt;
        kmemrt_status(&rt);
        kputs("\nmemrt hot/warm/cold: ");
        print_u32(rt.hot_bytes); kputs("/");
        print_u32(rt.warm_bytes); kputs("/");
        print_u32(rt.cold_bytes);
        kputs("\nmemrt pressure/frag/locality/numa: ");
        print_u32(rt.pressure_score); kputs("/");
        print_u32(rt.fragmentation_score); kputs("/");
        print_u32(rt.locality_score); kputs("/");
        print_u32(rt.numa_node);
        kputs("\nmemrt reclaim/compress/migrate: ");
        print_u32(rt.reclaim_events); kputs("/");
        print_u32(rt.compression_events); kputs("/");
        print_u32(rt.migration_events);
        kputs("\nmemrt vault slots/bytes zc rowgap pred: ");
        print_u32(rt.vault_slots); kputs("/");
        print_u32(rt.vault_bytes); kputs(" ");
        print_u32(rt.zero_copy_arenas); kputs(" ");
        print_u32(rt.anti_rowhammer_gaps); kputs(" ");
        print_u32(rt.predicted_alloc);
    }
    kputc('\n');
}

static void cmd_cpuinfo(const char* args) {
    (void)args;
    const cpu_info_t* c = kcpuinfo();
    kputs("vendor: ");
    kputs(c->vendor);
    kputs("\ncores: ");
    print_u32(c->logical_cores);
    kputc('\n');
}

static void print_hex16_local(u16 value) {
    static const char hex[] = "0123456789ABCDEF";
    kputc(hex[(value >> 12u) & 0x0Fu]);
    kputc(hex[(value >> 8u) & 0x0Fu]);
    kputc(hex[(value >> 4u) & 0x0Fu]);
    kputc(hex[value & 0x0Fu]);
}

static void print_hex32_local(u32 value) {
    static const char digits[] = "0123456789ABCDEF";
    for (i32 shift = 28; shift >= 0; shift -= 4) {
        kputc(digits[(value >> (u32)shift) & 0x0Fu]);
    }
}

static void cmd_systeminfo(const char* args) {
    const cpu_info_t* cpu;
    kernel_metrics_t metrics;
    netstack_status_t net;
    hw_topology_status_t topology;
    char ip_text[32];
    char hostname[32];
    u32 pci_count = 0u;
    u32 vbox_devices = 0u;
    u32 disk_total = kdisk_count();
    (void)args;

    cpu = kcpuinfo();
    ksysinfo(&metrics);
    knetstack_status(&net);
    khw_topology_status(&topology);

    if (kpersist_get_hostname(hostname, sizeof(hostname)) != 0) str_cpy(hostname, "axiom");
    kputs("Host Name:                     "); kputs(hostname); kputc('\n');
    kputs("OS Name:                       Axiom LastOS\n");
    kputs("OS Version:                    0.1\n");
    kputs("OS Architecture:              x86 protected mode\n");
    kputs("System Type:                  ");
    kputs(cpu->has_long_mode ? "x86_64-capable CPU\n" : "x86 CPU\n");
    kputs("Processor:                    ");
    kputs(cpu->brand[0] ? cpu->brand : cpu->vendor);
    kputs("\nCPU Vendor:                   "); kputs(cpu->vendor);
    kputs("\nCPU Family/Model/Stepping:    ");
    print_u32(cpu->family); kputc('/'); print_u32(cpu->model); kputc('/'); print_u32(cpu->stepping);
    kputs("\nLogical/Physical Cores:       ");
    print_u32(cpu->logical_cores); kputc('/'); print_u32(cpu->physical_cores);
    kputs("\nCache L2/L3:                  ");
    print_u32(cpu->l2_cache_kb); kputs(" KB/"); print_u32(cpu->l3_cache_kb); kputs(" KB\n");
    kputs("Total Physical Memory:        "); print_u32(metrics.ram_bytes / 1024u); kputs(" KB\n");
    kputs("Estimated Available Memory:   ");
    print_u32(metrics.ram_bytes > metrics.heap_used ? (metrics.ram_bytes - metrics.heap_used) / 1024u : 0u);
    kputs(" KB\n");
    kputs("System Boot Ticks:            "); print_u32(metrics.ticks); kputc('\n');
    kputs("Timer Frequency:              "); print_u32(metrics.timer_hz); kputs(" Hz\n");
    kputs("Topology Cores/NUMA:           "); print_u32(topology.core_count); kputc('/'); print_u32(topology.numa_nodes); kputc('\n');
    kputs("APIC:                         "); kputs(cpu->has_apic ? "Present\n" : "Not detected\n");
    kputs("NX/SMEP/SMAP:                ");
    kputs(cpu->has_nx ? "on/" : "off/"); kputs(cpu->has_smep ? "on/" : "off/"); kputs(cpu->has_smap ? "on\n" : "off\n");

    kputs("\nNetwork Adapter:              "); kputs(nic_backend_name()); kputs(nic_ready() ? " ready\n" : " not ready\n");
    kputs("VirtIO network driver:        "); kputs(virtio_net_ready() ? "ready\n" : "not detected\n");
    ip_format_ipv4(net.local_ip, ip_text, sizeof(ip_text));
    kputs("IP Address:                   "); kputs(ip_text);
    ip_format_ipv4(net.local_gw, ip_text, sizeof(ip_text));
    kputs("\nGateway:                      "); kputs(ip_text);
    kputs("\nARP Entries:                  "); print_u32(net.arp_entries); kputc('\n');
    kputs("NIC RX Frames:                "); print_u32(net.frames_rx); kputc('\n');

    kputs("\nStorage Devices:              "); print_u32(disk_total); kputc('\n');
    for (u32 i = 0u; i < disk_total; i++) {
        disk_info_t disk;
        if (kdisk_info(i, &disk) != 0) continue;
        kputs("  Disk["); print_u32(i); kputs("]: ");
        print_u32(disk.total_sectors_low / 2048u); kputs(" MB, ");
        kputs(disk.persistent ? "persistent" : "volatile");
        kputs(disk.writable ? ", writable" : ", read-only");
        kputs(", backend=");
        if (disk.backend == 5u) kputs("NVMe");
        else if (disk.backend == 3u) kputs("VirtIO-blk");
        else if (disk.backend == 2u) kputs("ATA");
        else if (disk.backend == 1u) kputs("RAM");
        else if (disk.backend == 4u) kputs("USB Mass Storage");
        else kputs("none");
        kputc('\n');
    }

    kputs("VirtIO block driver:           "); kputs(virtio_blk_ready() ? "ready\n" : "not detected\n");
    kputs("USB controller:                ");
    { usb_status_t usb; usb_status(&usb); kputs(usb.controller_ready ? "OHCI ready" : "not ready");
      kputs(" (HID keyboard="); kputs(usb.hid_keyboard ? "yes" : "no"); kputs(", mass-storage=");
      kputs(usb.mass_storage_ready ? "ready" : (usb.mass_storage_found ? "detected/not ready" : "not detected")); kputs(")\n"); }
    kputs("Framebuffer:                   "); kputs(gfx_backend_name()); kputs(" ");
    print_u32(gfx_width()); kputc('x'); print_u32(gfx_height()); kputs("x"); print_u32(gfx_bpp()); kputs(" bpp\n");

    kputs("\nPCI Devices:                 ");
    for (u32 bus = 0u; bus < 32u; bus++) {
        for (u32 slot = 0u; slot < 32u; slot++) {
            for (u32 func = 0u; func < 8u; func++) {
                u32 id = pci_config_read32((u8)bus, (u8)slot, (u8)func, 0u);
                if ((id & 0xFFFFu) == 0xFFFFu) continue;
                pci_count++;
                if ((id & 0xFFFFu) == 0x80EEu) vbox_devices++;
            }
        }
    }
    print_u32(pci_count); kputc('\n');
    kputs("VirtualBox PCI devices:        "); print_u32(vbox_devices); kputc('\n');
    for (u32 bus = 0u; bus < 32u; bus++) {
        for (u32 slot = 0u; slot < 32u; slot++) {
            for (u32 func = 0u; func < 8u; func++) {
                u32 id = pci_config_read32((u8)bus, (u8)slot, (u8)func, 0u);
                if ((id & 0xFFFFu) == 0xFFFFu) continue;
                kputs("  PCI "); print_u32(bus); kputc(':'); print_u32(slot); kputc('.'); print_u32(func);
                kputs(" vendor=0x"); print_hex16_local((u16)(id & 0xFFFFu));
                kputs(" device=0x"); print_hex16_local((u16)(id >> 16u)); kputc('\n');
            }
        }
    }
}

static void cmd_meminfo(const char* args) {
    args = skip_space(args);
    kernel_metrics_t m;
    ksysinfo(&m);
    kputs("ram bytes: ");
    print_u32(m.ram_bytes);
    kputs("\nheap used: ");
    print_u32(m.heap_used);
    kputs("\nheap peak: ");
    print_u32(m.heap_peak);
    kputs("\nheap corruption count: ");
    print_u32(m.heap_corruptions);
    kputs("\nheap fragmentation %: ");
    print_u32(m.heap_fragmentation);
    kputs("\nswap used kb: ");
    print_u32(m.swap_used_kb);
    kputs("\noom kills: ");
    print_u32(m.oom_kills);
    kputc('\n');

    if (str_cmp(args, "diag") == 0) {
        resource_snapshot_t rs;
        kresmon(&rs);
        kputs("frame largest order: ");
        print_u32(kframe_largest_order());
        kputs("\nframe fragmentation %: ");
        print_u32(kframe_fragmentation());
        kputs("\nalloc failures: ");
        print_u32(kadv_alloc_failures());
        kputs("\nreclaim hint bytes: ");
        print_u32(rs.reclaim_hint_bytes);
        kputc('\n');

        u32 cc = kslab_class_count();
        kputs("slab classes: ");
        print_u32(cc);
        kputc('\n');
        for (u32 i = 0; i < cc; i++) {
            slab_class_stats_t st;
            if (kslab_class_stats(i, &st) != 0) continue;
            kputs("  class=");
            print_u32(st.class_size);
            kputs(" total/free/regions=");
            print_u32(st.total_objects); kputs("/");
            print_u32(st.free_objects); kputs("/");
            print_u32(st.regions);
            kputc('\n');
        }
    }
}

static void cmd_vmstat(const char* args) {
    (void)args;
    vm_stats_t vs;
    kvm_stats(&vs);
    kputs("vm regions active/total: ");
    print_u32(vs.active_regions); kputs("/");
    print_u32(vs.total_regions);
    kputs("\nvm faults total/handled/protect/guard/oom: ");
    print_u32(vs.total_faults); kputs("/");
    print_u32(vs.handled_faults); kputs("/");
    print_u32(vs.protection_faults); kputs("/");
    print_u32(vs.guard_faults); kputs("/");
    print_u32(vs.oom_faults);
    kputs("\nvm committed pages: ");
    print_u32(vs.committed_pages);
    kputs("\nvm cow maps/splits: ");
    print_u32(vs.cow_maps); kputs("/");
    print_u32(vs.cow_splits);
    kputc('\n');
}

static void cmd_memlab(const char* args) {
    char t0[24];
    char t1[24];
    char t2[24];
    const char* p = skip_space(args);
    p = read_token(p, t0, sizeof(t0));

    if (!t0[0] || str_cmp(t0, "status") == 0) {
        memlab_snapshot_t st;
        kmemlab_status(&st);
        kputs("memlab samples/window: ");
        print_u32(st.samples_collected);
        kputs("/");
        print_u32(st.sample_window);
        kputs("\nheap avg used/frg: ");
        print_u32(st.heap_used_avg);
        kputs(" / ");
        print_u32(st.heap_frag_avg);
        kputs("%\nframe avg used/frg: ");
        print_u32(st.frame_used_avg);
        kputs(" / ");
        print_u32(st.frame_frag_avg);
        kputs("%\nframe largest order: ");
        print_u32(st.frame_largest_order);
        kputs("\nslab free/total: ");
        print_u32(st.slab_free_objects);
        kputs("/");
        print_u32(st.slab_total_objects);
        kputs("\nalloc failures total/delta: ");
        print_u32(st.alloc_failures_total);
        kputs("/");
        print_u32(st.alloc_failures_delta);
        kputs("\nfrag peak: ");
        print_u32(st.fragmentation_peak);
        kputs("%\npressure/risk/health: ");
        print_u32(st.pressure_score);
        kputs("/");
        print_u32(st.risk_score);
        kputs("/");
        print_u32(st.health_score);
        kputc('\n');
        return;
    }

    if (str_cmp(t0, "validate") == 0) {
        memlab_validation_t vr;
        int rc = kmemlab_validate(&vr);
        kputs("memlab validate: ");
        kputs(rc == 0 ? "OK" : "FAIL");
        kputs(" checks ");
        print_u32(vr.checks_passed);
        kputs("/");
        print_u32(vr.checks_total);
        kputs(" failed=");
        print_u32(vr.checks_failed);
        if (vr.first_error) {
            kputs(" first_err=");
            print_u32(vr.first_error);
        }
        kputc('\n');
        return;
    }

    if (str_cmp(t0, "reset") == 0) {
        kmemlab_reset();
        kputs("memlab reset done.\n");
        return;
    }

    if (str_cmp(t0, "run") == 0) {
        u32 scenario = 0u;
        u32 rounds = 256u;
        u32 payload = 512u;
        char tsc[24];
        p = read_token(p, tsc, sizeof(tsc));
        p = read_token(p, t1, sizeof(t1));
        p = read_token(p, t2, sizeof(t2));
        if (tsc[0]) (void)parse_u32(tsc, &scenario);
        if (t1[0]) (void)parse_u32(t1, &rounds);
        if (t2[0]) (void)parse_u32(t2, &payload);

        memlab_run_result_t rr;
        int rc = kmemlab_run(scenario, rounds, payload, &rr);
        kputs("memlab run scenario=");
        print_u32(rr.scenario);
        kputs(" rounds=");
        print_u32(rr.rounds);
        kputs(" ops=");
        print_u32(rr.operations);
        kputs(" fail=");
        print_u32(rr.failures);
        kputs("\nbytes req/got=");
        print_u32(rr.bytes_requested);
        kputs("/");
        print_u32(rr.bytes_allocated);
        kputs(" risk ");
        print_u32(rr.before_risk);
        kputs("->");
        print_u32(rr.after_risk);
        kputs("\nresult: ");
        kputs(rc == 0 ? "OK\n" : "PARTIAL/FAIL\n");
        return;
    }

    if (str_cmp(t0, "hist") == 0) {
        u32 max_bins = 16u;
        p = read_token(p, t1, sizeof(t1));
        if (t1[0]) {
            (void)parse_u32(t1, &max_bins);
        }
        u32 bins = kmemlab_hist_bins();
        if (max_bins > bins) max_bins = bins;
        kputs("memlab hist (fragmentation bins)\n");
        for (u32 i = 0u; i < max_bins; i++) {
            print_u32(i);
            kputs(": ");
            print_u32(kmemlab_hist_bin(i));
            kputc('\n');
        }
        return;
    }

    kputs("Usage: memlab status|validate|reset|run <scenario 0..4> [rounds] [payload]|hist [bins]\n");
}

static void cmd_save(const char* args) {
    if (!args || !args[0]) {
        kputs("Usage: save <text>\n");
        return;
    }
    kpersist_save(args);
    kputs("Saved persistently.\n");
}

static void cmd_load(const char* args) {
    (void)args;
    char data[64];
    kpersist_load(data, sizeof(data));
    if (!data[0]) {
        kputs("(empty)\n");
        return;
    }
    kputs(data);
    kputc('\n');
}

static void cmd_bootcount(const char* args) {
    (void)args;
    kputs("boot count: ");
    print_u32(kboot_count());
    kputc('\n');
}

static void cmd_copy(const char* args) {
    if (!args || !args[0]) {
        kputs("Usage: copy <text>\n");
        return;
    }
    kclip_set(args);
    kputs("Copied.\n");
}

static void cmd_paste(const char* args) {
    (void)args;
    char clip[128];
    kclip_get(clip, sizeof(clip));
    if (!clip[0]) {
        kputs("(clipboard empty)\n");
        return;
    }
    kputs(clip);
    kputc('\n');
}

static void cmd_patchtest(const char* args) {
    (void)args;
    int rc = kpatch_selftest();
    kputs(rc == 0 ? "Patch self-test: OK\n" : "Patch self-test: FAILED\n");
}

static void cmd_selftest(const char* args) {
    selftest_result_t r;
    char t0[16];
    u32 rounds = 0;
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "status") == 0) {
        kselftest_status(&r);
        kputs("selftest total/pass/fail: ");
        print_u32(r.total); kputs("/");
        print_u32(r.passed); kputs("/");
        print_u32(r.failed);
        kputs("\nlast step/error/tick: ");
        print_u32(r.last_step); kputs("/");
        print_u32(r.last_error); kputs("/");
        print_u32(r.last_tick);
        kputc('\n');
        return;
    }
    if (str_cmp(args, "full") == 0) {
        int rc = kselftest_full(&r);
        kputs("selftest full total/pass/fail: ");
        print_u32(r.total); kputs("/");
        print_u32(r.passed); kputs("/");
        print_u32(r.failed);
        kputs("\nlast step/error/tick: ");
        print_u32(r.last_step); kputs("/");
        print_u32(r.last_error); kputs("/");
        print_u32(r.last_tick);
        kputc('\n');
        if (rc == 0) kputs("selftest result: PASS\n");
        else kputs("selftest result: FAIL\n");
        return;
    }
    if (starts_with(args, "stress")) {
        args += 6;
        args = skip_space(args);
        if (args[0]) {
            (void)read_token(args, t0, sizeof(t0));
            (void)parse_u32(t0, &rounds);
        }
        if (selftest_run_stress(rounds, &r) == 0) {
            kputs("selftest stress PASS ");
        } else {
            kputs("selftest stress FAIL ");
        }
        print_u32(r.passed); kputs("/"); print_u32(r.failed); kputc('\n');
        return;
    }
    if (starts_with(args, "fuzz")) {
        kputs("selftest fuzz is not available in this build\n");
        return;
    }
    kputs("Usage: selftest status|full|stress [rounds]|fuzz [rounds]\n");
}

static void cmd_betatest(const char* args) {
    (void)args;
    selftest_result_t self;
    vfs_fsck_report_t fsck;
    int self_rc = kselftest_full(&self);
    int fs_rc = kvfs_fsck(&fsck, 0u);
    kputs("betatest selftest: ");
    kputs(self_rc == 0 ? "PASS\n" : "FAIL\n");
    kputs("betatest vfs: ");
    if (fs_rc == 0 && fsck.bad_nodes == 0u && fsck.bad_references == 0u) {
        kputs("PASS\n");
    } else {
        kputs("FAIL\n");
    }
    kputs("betatest result: ");
    kputs(self_rc == 0 && fs_rc == 0 && fsck.bad_nodes == 0u && fsck.bad_references == 0u ? "READY\n" : "CHECK REQUIRED\n");
}

static void cmd_watchdog(const char* args) {
    char t0[16], t1[16], t2[16], t3[16];
    watchdog_status_t ws;
    watchdog_policy_t wp;
    args = skip_space(args);

    if (!args[0] || str_cmp(args, "status") == 0) {
        kwatchdog_status(&ws);
        kputs("watchdog healthy/stalls/heapfaults/oom/actions/process-recoveries: ");
        print_u32(ws.healthy); kputs("/");
        print_u32(ws.stalls_detected); kputs("/");
        print_u32(ws.heap_faults); kputs("/");
        print_u32(ws.vm_oom_spikes); kputs("/");
        print_u32(ws.mitigation_actions); kputs("/");
        print_u32(ws.process_recoveries);
        kputs("\nlast switch tick: ");
        print_u32(ws.last_switch_tick);
        kputs(" now tick: ");
        print_u32(ws.ticks);
        kputc('\n');
        return;
    }

    if (str_cmp(args, "policy") == 0) {
        kwatchdog_policy(&wp);
        kputs("watchdog policy stall/oom/heap/auto: ");
        print_u32(wp.stall_threshold_ticks); kputs("/");
        print_u32(wp.vm_oom_spike_threshold); kputs("/");
        print_u32(wp.heap_check_period); kputs("/");
        print_u32(wp.auto_mitigate);
        kputc('\n');
        return;
    }

    if (starts_with(args, "set ")) {
        args += 4;
        args = read_token(args, t0, sizeof(t0));
        args = read_token(args, t1, sizeof(t1));
        args = read_token(args, t2, sizeof(t2));
        (void)read_token(args, t3, sizeof(t3));

        if (parse_u32(t0, &wp.stall_threshold_ticks) != 0 ||
            parse_u32(t1, &wp.vm_oom_spike_threshold) != 0 ||
            parse_u32(t2, &wp.heap_check_period) != 0 ||
            parse_u32(t3, &wp.auto_mitigate) != 0) {
            kputs("Usage: watchdog set <stall_ticks> <oom_spike> <heap_period> <auto0|1>\n");
            return;
        }
        kwatchdog_set_policy(&wp);
        kputs("watchdog policy updated.\n");
        return;
    }

    kputs("Usage: watchdog status|policy|set <stall_ticks> <oom_spike> <heap_period> <auto0|1>\n");
}

static void cmd_paniclog(const char* args) {
    char tok[16];
    u32 n = 5u;
    panic_record_t r;
    args = skip_space(args);

    if (!args[0] || str_cmp(args, "status") == 0) {
        kputs("paniclog count: ");
        print_u32(kpaniclog_count());
        kputc('\n');
        return;
    }

    if (starts_with(args, "show")) {
        args += 4;
        args = skip_space(args);
        if (args[0]) {
            (void)read_token(args, tok, sizeof(tok));
            if (parse_u32(tok, &n) != 0) n = 5u;
        }
        if (n > 16u) n = 16u;
        for (u32 i = 0; i < n; i++) {
            if (kpaniclog_get(i, &r) != 0) break;
            kputs("#"); print_u32(r.id);
            kputs(" task="); print_u32(r.task_id);
            kputs(" tick="); print_u32(r.tick);
            kputs(" int="); print_u32(r.int_no);
            kputs(" err="); print_u32(r.err_code);
            kputs(" eip="); print_u32(r.eip);
            kputs(" addr="); print_u32(r.addr_hint);
            kputs(" hc/fr="); print_u32(r.heap_corruptions); kputs("/"); print_u32(r.heap_fragmentation);
            kputs(" wd="); print_u32(r.watchdog_stalls); kputs("/"); print_u32(r.watchdog_actions);
            if (r.msg[0]) { kputs(" msg="); kputs(r.msg); }
            kputc('\n');
        }
        return;
    }

    if (str_cmp(args, "clear") == 0) {
        kpaniclog_clear();
        kputs("paniclog cleared.\n");
        return;
    }

    kputs("Usage: paniclog status|show [n]|clear\n");
}

static void cmd_antivirus(const char* args) {
    antivirus_status_t av;
    ai_security_status_t ai;
    args = skip_space(args);

    kantivirus_status(&av);
    kai_security_status(&ai);

    if (!args[0] || str_cmp(args, "status") == 0) {
        kputs("antivirus status\n");
        kputs("Malware: ");
        print_u32(av.active_threats);
        kputs(" active threats\nQuarantine: ");
        print_u32(av.quarantined_processes);
        kputs(" processes\nNetwork anomalies: ");
        print_u32(av.network_anomalies);
        kputs("\nFilesystem: ");
        print_u32(av.filesystem_integrity_percent);
        kputs("% integrity\nSignatures v");
        print_u32(av.signature_version);
        kputs(" heuristics v");
        print_u32(av.heuristic_version);
        kputc('\n');
        return;
    }

    if (str_cmp(args, "ai") == 0) {
        kputs("ai security\nrisk score: ");
        print_u32(ai.risk_score);
        kputs("\npredicted threat window: ");
        print_u32(ai.predicted_threat_window);
        kputs("\nfirewall level: ");
        print_u32(ai.adaptive_firewall_level);
        kputs("\nlearning epoch: ");
        print_u32(ai.learning_epoch);
        kputc('\n');
        return;
    }

    if (str_cmp(args, "suggest") == 0) {
        if (ai.risk_score > 220 || av.active_threats > 2) {
            kputs("AI Suggestion: switch to server mode, tighten firewall, isolate suspicious modules.\n");
        } else if (ai.risk_score > 100 || av.network_anomalies > 4) {
            kputs("AI Suggestion: keep balanced mode, enable deeper scan and review threatlog.\n");
        } else {
            kputs("AI Suggestion: system stable, keep desktop-light monitoring.\n");
        }
        return;
    }

    kputs("Usage: antivirus status|ai|suggest\n");
}

static void cmd_threatlog(const char* args) {
    (void)args;
    shell_threatlog_print();
}

static void cmd_processinfo(const char* args) {
    (void)args;
    shell_processinfo_print();
}

static void cmd_ps(const char* args) {
    (void)args;
    u32 count = kprocess_count();
    kputs("PID  TICKS  BLOCKED  NAME\n");
    for (u32 i = 0u; i < count; i++) {
        process_info_t p;
        if (kprocess_at(i, &p) != 0) continue;
        print_u32(p.task_id);
        kputs("  ");
        print_u32(p.runtime_ticks);
        kputs("  ");
        print_u32(p.blocked ? 1u : 0u);
        kputs("       ");
        kputs(p.name[0] ? p.name : "task");
        kputc('\n');
    }
}

static void cmd_kill(const char* args) {
    char token[16];
    u32 task = 0u;
    args = read_token(skip_space(args), token, sizeof(token));
    (void)args;
    if (!token[0] || parse_u32(token, &task) != 0) {
        kputs("Usage: kill <pid>\n");
        return;
    }
    if (task == kprocess_current()) {
        kputs("kill: refusing to terminate the current shell task\n");
        return;
    }
    if (ksched_kill(task) != 0) {
        kputs("kill: process not found or cannot be terminated\n");
        return;
    }
    kputs("terminated pid ");
    print_u32(task);
    kputc('\n');
}

static void cmd_exec(const char* args) {
    args = skip_space(args);
    if (!args[0]) {
        kputs("Usage: exec <built-in command>\n");
        return;
    }
    /* This is a real shell exec path, not an ELF process replacement yet. */
    (void)commands_execute_status(args);
}

static void cmd_smp(const char* args) {
    args = skip_space(args);
    if (args[0] && str_cmp(args, "status") != 0) {
        kputs("Usage: smp status\n");
        return;
    }
    smp_status_t s;
    ksmp_status(&s);
    kputs("smp detected/started/online: ");
    print_u32(s.detected); kputc('/'); print_u32(s.started); kputc('/'); print_u32(s.online);
    kputs(" lapic=0x"); print_u32(s.lapic_addr); kputc('\n');
}

static void cmd_spawn(const char* args) {
    char path[VFS_PATH_MAX];
    args = read_token(skip_space(args), path, sizeof(path));
    u32 pid = 0u;
    if (!path[0]) {
        kputs("Usage: spawn <file.elf>\n");
        return;
    }
    if (kscheduler_create_user_task(path, &pid) != 0) {
        kputs("spawn: no free process slot\n");
        return;
    }

    u32 fd = 0u;
    vfs_node_info_t info;
    if (kfile_open(path, KFILE_READ, &fd) != 0 || kfile_stat(fd, &info) != 0) {
        (void)kfile_close(fd);
        (void)ksched_kill(pid);
        kputs("spawn: cannot open ELF file\n");
        return;
    }
    if (info.size > VFS_FILE_LIMIT) {
        kfile_close(fd);
        kputs("spawn: ELF is too large for the loader buffer\n");
        return;
    }
    u32 total = 0u;
    while (total < info.size) {
        u32 want = info.size - total;
        if (want > 512u) want = 512u;
        int n = kfile_read(fd, spawn_image + total, want);
        if (n <= 0) {
            kfile_close(fd);
            (void)ksched_kill(pid);
            kputs("spawn: short read at "); print_u32(total);
            kputs("/"); print_u32(info.size); kputc('\n');
            return;
        }
        total += (u32)n;
    }
    kfile_close(fd);

    elf_exec_info_t exec;
    if (kelf_load_user(spawn_image, total, pid, &exec) != 0) {
        (void)ksched_kill(pid);
        kputs("spawn: invalid ELF or user mapping failed\n");
        return;
    }
    kputs("spawn: ring3 task="); print_u32(pid);
    kputs(" entry="); print_u64(exec.entry);
    kputs(" pages="); print_u32(exec.mapped_pages);
    kputs(" stack="); print_u64(exec.stack_bottom);
    kputs("-"); print_u64(exec.stack_top);
    kputc('\n');
    g_last_spawn_pid = pid;
    kscheduler_request_user_switch();
}

static void cmd_usertest(const char* args) {
    (void)args;
    cmd_spawn("/bin/usermode-test.elf");
}

static void cmd_gfxdemo(const char* args) {
    (void)args;
    kputs("gfxdemo: pure kernel mode (GUI apps run from userland)\n");
}

static void cmd_gui(const char* args) {
    (void)args;
    kputs("gui: pure kernel mode (GUI apps run from userland)\n");
}

void shell_autostart_gui(void) {
}

static int command_contains(const char* text, u32 text_len, const char* needle) {
    u32 needle_len = str_len(needle);
    if (!needle_len) return 1;
    if (needle_len > text_len) return 0;
    for (u32 i = 0u; i + needle_len <= text_len; i++) {
        if (str_ncmp(text + i, needle, needle_len) == 0) return 1;
    }
    return 0;
}

static void cmd_grep(const char* args) {
    char pattern[64];
    args = read_token(skip_space(args), pattern, sizeof(pattern));
    (void)args;
    if (!pattern[0]) {
        kputs("Usage: grep <text>\n");
        return;
    }
    if (!command_pipe_input) {
        kputs("grep: no pipe input\n");
        return;
    }
    u32 line_start = 0u;
    for (u32 i = 0u; i <= command_pipe_input_length; i++) {
        if (i == command_pipe_input_length || command_pipe_input[i] == '\n') {
            u32 line_len = i - line_start;
            while (line_len && command_pipe_input[line_start + line_len - 1u] == '\r') line_len--;
            if (command_contains(command_pipe_input + line_start, line_len, pattern)) {
                for (u32 j = 0u; j < line_len; j++) kputc(command_pipe_input[line_start + j]);
                kputc('\n');
            }
            line_start = i + 1u;
        }
    }
}

static void cmd_procctl(const char* args) {
    char t0[16], t1[16];
    u32 task = 0, parent = 0;
    i32 code = 0;
    proc_status_t st;

    args = skip_space(args);
    if (!args[0]) {
        kputs("Usage: procctl signal <task> <term|kill|usr1|usr2>|procctl wait <task>|procctl parent <task> <parent>|procctl show <task>\n");
        return;
    }

    if (starts_with(args, "signal ")) {
        proc_signal_t sig = PROC_SIG_NONE;
        args += 7;
        args = read_token(args, t0, sizeof(t0));
        (void)read_token(args, t1, sizeof(t1));
        if (parse_u32(t0, &task) != 0 || !t1[0]) {
            kputs("Usage: procctl signal <task> <term|kill|usr1|usr2>\n");
            return;
        }
        if (str_cmp(t1, "term") == 0) sig = PROC_SIG_TERM;
        else if (str_cmp(t1, "kill") == 0) sig = PROC_SIG_KILL;
        else if (str_cmp(t1, "usr1") == 0) sig = PROC_SIG_USR1;
        else if (str_cmp(t1, "usr2") == 0) sig = PROC_SIG_USR2;
        else {
            kputs("Usage: procctl signal <task> <term|kill|usr1|usr2>\n");
            return;
        }
        if (kproc_signal_send(task, sig) != 0) kputs("signal failed\n");
        else kputs("signal queued\n");
        return;
    }

    if (starts_with(args, "wait ")) {
        args += 5;
        (void)read_token(args, t0, sizeof(t0));
        if (parse_u32(t0, &task) != 0) {
            kputs("Usage: procctl wait <task>\n");
            return;
        }
        if (kproc_wait(task, &code) == 0) {
            kputs("exit code=");
            print_u32((u32)code);
            kputc('\n');
        } else {
            kputs("not exited yet\n");
        }
        return;
    }

    if (starts_with(args, "parent ")) {
        args += 7;
        args = read_token(args, t0, sizeof(t0));
        (void)read_token(args, t1, sizeof(t1));
        if (parse_u32(t0, &task) != 0 || parse_u32(t1, &parent) != 0) {
            kputs("Usage: procctl parent <task> <parent>\n");
            return;
        }
        kproc_set_parent(task, parent);
        kputs("parent updated\n");
        return;
    }

    if (starts_with(args, "show ")) {
        args += 5;
        (void)read_token(args, t0, sizeof(t0));
        if (parse_u32(t0, &task) != 0) {
            kputs("Usage: procctl show <task>\n");
            return;
        }
        if (kproc_status(task, &st) != 0) {
            kputs("process not found\n");
            return;
        }
        kputs("task/parent/alive/zombie: ");
        print_u32(st.task_id); kputs("/");
        print_u32(st.parent_task); kputs("/");
        print_u32(st.alive); kputs("/");
        print_u32(st.zombie);
        kputs("\npending/lastsig/waiters/exit: ");
        print_u32(st.pending_signals); kputs("/");
        print_u32(st.last_signal); kputs("/");
        print_u32(st.waiters); kputs("/");
        print_u32((u32)st.exit_code);
        kputc('\n');
        return;
    }

    kputs("Usage: procctl signal <task> <term|kill|usr1|usr2>|procctl wait <task>|procctl parent <task> <parent>|procctl show <task>\n");
}

static void cmd_lastmap(const char* args) {
    args = skip_space(args);
    if (starts_with(args, "scan ")) {
        args = skip_space(args + 5);
    } else if (str_cmp(args, "report") == 0) {
        (void)klastmap("--stats");
        return;
    }

    int rc = klastmap(args);
    if (rc < 0) {
        kputs("lastmap failed. use: lastmap --adv -p 1-100 192.168.1.10\n");
        return;
    }
    resource_snapshot_t s;
    kresmon(&s);
    ai_assist_hint("lastmap", &s);
}

static void cmd_lastscan(const char* args) {
    args = skip_space(args);
    if (!args[0]) {
        kputs("Usage: lastscan <target_ip>\n");
        return;
    }
    if (args[0] == '-') {
        kputs("Simple mode only. use lastmap --adv for advanced flags.\n");
        return;
    }
    if (klastmap(args) < 0) {
        kputs("lastscan failed.\n");
        return;
    }
    resource_snapshot_t s;
    kresmon(&s);
    ai_assist_hint("lastmap", &s);
}

static void cmd_lastshark(const char* args) {
    args = skip_space(args);
    if (str_cmp(args, "analyze") == 0) {
        (void)klastshark("stats");
        (void)klastshark("show 10");
        return;
    }
    if (klastshark(args) != 0) {
        kputs("Usage: lastshark start|stop|stats|show [n]|filter all|tcp|udp|icmp|clear\n");
    }
}

static void cmd_firewall(const char* args) {
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "list") == 0) {
        kputs("FIREWALL RULES\n");
        for (u32 i = 0; i < FW_RULE_MAX; i++) {
            if (!fw_rules[i].used) {
                continue;
            }
            print_u32(fw_rules[i].port);
            kputs(" ");
            kputs(fw_rules[i].allow ? "ALLOW\n" : "DENY\n");
        }
        return;
    }

    int is_allow = 0;
    if (starts_with(args, "allow ")) {
        is_allow = 1;
        args += 6;
    } else if (starts_with(args, "deny ")) {
        is_allow = 0;
        args += 5;
    } else {
        kputs("Usage: firewall allow <port> | firewall deny <port> | firewall list\n");
        return;
    }

    u32 p = 0;
    if (parse_u32(args, &p) != 0 || p == 0 || p > 65535u) {
        kputs("Invalid port.\n");
        return;
    }

    for (u32 i = 0; i < FW_RULE_MAX; i++) {
        if (fw_rules[i].used && fw_rules[i].port == (u16)p) {
            fw_rules[i].allow = (u8)is_allow;
            kputs("Rule updated.\n");
            return;
        }
    }
    for (u32 i = 0; i < FW_RULE_MAX; i++) {
        if (!fw_rules[i].used) {
            fw_rules[i].used = 1;
            fw_rules[i].port = (u16)p;
            fw_rules[i].allow = (u8)is_allow;
            kputs("Rule added.\n");
            return;
        }
    }
    kputs("Firewall table full.\n");
}

static void cmd_module(const char* args) {
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "list") == 0) {
        for (u32 i = 0; i < kmodule_count(); i++) {
            module_info_t mi;
            if (kmodule_get_by_index(i, &mi) != 0) {
                continue;
            }
            kputs(mi.name);
            kputs(" ");
            kputs(mi.loaded ? "loaded" : "unloaded");
            kputs(" trust=");
            if (mi.trust == MODULE_TRUST_CORE) kputs("core");
            else if (mi.trust == MODULE_TRUST_TRUSTED) kputs("trusted");
            else if (mi.trust == MODULE_TRUST_COMMUNITY) kputs("community");
            else kputs("untrusted");
            kputs(" v=");
            print_u32(mi.version);
            kputs(" sig=");
            print_u32(mi.verified ? 1u : 0u);
            kputs(" rb=");
            print_u32(mi.rollback_count);
            kputs(" count=");
            print_u32(mi.load_count);
            kputc('\n');
        }
        return;
    }
    if (starts_with(args, "load ")) {
        args += 5;
        if (kmodule_load(args) == 0) {
            kputs("Module loaded.\n");
        } else {
            kputs("Unknown module.\n");
        }
        return;
    }
    if (starts_with(args, "install ")) {
        args += 8;
        if (kmodule_install(args) == 0) {
            kputs("Module installed.\n");
        } else {
            kputs("Install failed.\n");
        }
        return;
    }
    if (starts_with(args, "unload ")) {
        args += 7;
        if (kmodule_unload(args) == 0) {
            kputs("Module unloaded.\n");
        } else {
            kputs("Unknown module.\n");
        }
        return;
    }
    if (starts_with(args, "uninstall ")) {
        args += 10;
        if (kmodule_uninstall(args) == 0) {
            kputs("Module uninstalled.\n");
        } else {
            kputs("Uninstall failed.\n");
        }
        return;
    }
    if (starts_with(args, "markgood ")) {
        args += 9;
        if (kmodule_mark_good(args) == 0) {
            kputs("Module signature marked as good.\n");
        } else {
            kputs("Mark-good failed.\n");
        }
        return;
    }
    if (starts_with(args, "rollback ")) {
        args += 9;
        if (kmodule_rollback(args) == 0) {
            kputs("Module rolled back to last known good signature.\n");
        } else {
            kputs("Rollback failed.\n");
        }
        return;
    }
    kputs("Usage: module list | module install <name> | module uninstall <name> | module load <name> | module unload <name> | module markgood <name> | module rollback <name>\n");
}


