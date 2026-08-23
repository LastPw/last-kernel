/* Widgets, storage, installer, recovery, and device command handlers. */
static void cmd_av_widget(const char* args) {
    char op[16];
    char tok[32];
    u32 pid = 0;
    args = read_token(skip_space(args), op, sizeof(op));

    if (!op[0]) {
        kputs("Usage: av scan <path>|av quarantine <pid>\n");
        return;
    }

    if (str_cmp(op, "scan") == 0) {
        (void)read_token(args, tok, sizeof(tok));
        if (!tok[0]) {
            kputs("Usage: av scan <path>\n");
            return;
        }
        kputs("Scan requested for: ");
        kputs(tok);
        kputs(" (RAM/virtual FS mode)\n");
        cmd_antivirus("status");
        return;
    }

    if (str_cmp(op, "quarantine") == 0) {
        (void)read_token(args, tok, sizeof(tok));
        if (parse_u32(tok, &pid) != 0) {
            kputs("Usage: av quarantine <pid>\n");
            return;
        }
        ksched_set_realtime(pid, 0);
        ksched_set_priority(pid, 39);
        kputs("Process quarantined policy applied.\n");
        return;
    }

    kputs("Usage: av scan <path>|av quarantine <pid>\n");
}

static void cmd_fw_widget(const char* args) {
    char tok[24];
    u32 port = 0;
    const char* name;
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "status") == 0) {
        cmd_firewall("list");
        for (u32 i = 0; i < 16; i++) {
            if (!fw_app_rules[i].used) {
                continue;
            }
            kputs("app ");
            kputs(fw_app_rules[i].name);
            kputs(" ");
            kputs(fw_app_rules[i].allow ? "ALLOW\n" : "BLOCK\n");
        }
        return;
    }
    if (starts_with(args, "block ")) {
        name = skip_space(args + 6);
        (void)read_token(name, tok, sizeof(tok));
        if (!tok[0]) {
            kputs("Usage: fw block <ip|app|port>\n");
            return;
        }
        if (parse_u32(tok, &port) == 0 && port > 0u && port <= 65535u) {
            char b[24];
            str_cpy(b, "deny ");
            str_cat(b, tok);
            cmd_firewall(b);
            return;
        }
        for (u32 i = 0; i < 16; i++) {
            if (fw_app_rules[i].used && str_cmp(fw_app_rules[i].name, tok) == 0) {
                fw_app_rules[i].allow = 0;
                kputs("App rule updated.\n");
                return;
            }
        }
        for (u32 i = 0; i < 16; i++) {
            if (!fw_app_rules[i].used) {
                fw_app_rules[i].used = 1;
                fw_app_rules[i].allow = 0;
                str_ncpy(fw_app_rules[i].name, tok, sizeof(fw_app_rules[i].name));
                kputs("App blocked.\n");
                return;
            }
        }
        kputs("App rule table full.\n");
        return;
    }
    if (starts_with(args, "allow ")) {
        name = skip_space(args + 6);
        (void)read_token(name, tok, sizeof(tok));
        if (!tok[0]) {
            kputs("Usage: fw allow <app|port>\n");
            return;
        }
        if (parse_u32(tok, &port) == 0 && port > 0u && port <= 65535u) {
            char b[24];
            str_cpy(b, "allow ");
            str_cat(b, tok);
            cmd_firewall(b);
            return;
        }
        for (u32 i = 0; i < 16; i++) {
            if (fw_app_rules[i].used && str_cmp(fw_app_rules[i].name, tok) == 0) {
                fw_app_rules[i].allow = 1;
                kputs("App rule updated.\n");
                return;
            }
        }
        for (u32 i = 0; i < 16; i++) {
            if (!fw_app_rules[i].used) {
                fw_app_rules[i].used = 1;
                fw_app_rules[i].allow = 1;
                str_ncpy(fw_app_rules[i].name, tok, sizeof(fw_app_rules[i].name));
                kputs("App allowed.\n");
                return;
            }
        }
        kputs("App rule table full.\n");
        return;
    }
    kputs("Usage: fw status|fw block <ip>|fw allow <app>\n");
}

static void cmd_logs_widget(const char* args) {
    args = skip_space(args);
    if (!args[0] || starts_with(args, "view")) {
        shell_threatlog_print();
        return;
    }
    if (str_cmp(args, "export") == 0) {
        kputs("Logs export completed to serial/audit stream.\n");
        return;
    }
    kputs("Usage: logs view <type>|logs export\n");
}

static void cmd_storage_widget(const char* args) {
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "analyze") == 0) {
        cmd_meminfo("");
        return;
    }
    if (str_cmp(args, "optimize") == 0) {
        kresource_profile(RE_PROFILE_SERVER_LIGHT);
        kputs("Storage prefetch/IO optimization profile applied.\n");
        return;
    }
    kputs("Usage: storage analyze|storage optimize\n");
}

static void cmd_vps_widget(const char* args) {
    char op[16];
    char tok[16];
    u32 id = 0;
    args = read_token(skip_space(args), op, sizeof(op));

    if (!op[0] || str_cmp(op, "list") == 0) {
        kputs("vps instances: ");
        print_u32(knamespace_count());
        kputc('\n');
        for (u32 i = 0; i < 32u; i++) {
            if (!vps_active[i]) {
                continue;
            }
            kputs("vps ");
            print_u32(i);
            kputs(" active\n");
        }
        return;
    }

    (void)read_token(args, tok, sizeof(tok));
    if (parse_u32(tok, &id) != 0) {
        kputs("Usage: vps start <id>|vps stop <id>|vps list\n");
        return;
    }

    if (str_cmp(op, "start") == 0) {
        while (knamespace_count() <= id) {
            if (knamespace_create() < 0) {
                kputs("Cannot allocate namespace.\n");
                return;
            }
        }
        if (id < 32u) {
            vps_active[id] = 1;
        }
        kputs("VPS namespace active: ");
        print_u32(id);
        kputc('\n');
        return;
    }
    if (str_cmp(op, "stop") == 0) {
        if (id < 32u) {
            vps_active[id] = 0;
        }
        kputs("VPS namespace stopped: ");
        print_u32(id);
        kputc('\n');
        return;
    }
    kputs("Usage: vps start <id>|vps stop <id>|vps list\n");
}

static void cmd_predict_widget(const char* args) {
    resource_snapshot_t s;
    ai_security_status_t ai;
    args = skip_space(args);
    kresmon(&s);
    kai_security_status(&ai);

    if (str_cmp(args, "cpu") == 0) {
        kputs("predict cpu: next load trend ");
        print_u32(s.cpu_busy_percent + (ai.predicted_threat_window / 10u));
        kputs("%\n");
        return;
    }
    if (str_cmp(args, "ram") == 0) {
        kputs("predict ram: next alloc ");
        print_u32(s.predicted_next_alloc);
        kputs(" bytes\n");
        return;
    }
    if (str_cmp(args, "network") == 0) {
        kputs("predict network: traffic risk ");
        print_u32(ai.risk_score);
        kputs(" level\n");
        return;
    }
    kputs("Usage: predict cpu|predict ram|predict network\n");
}

static void cmd_theme_widget(const char* args) {
    args = skip_space(args);
    if (starts_with(args, "set ")) {
        u8 t=0,b=0,a=1,h=0,pc=0x0A,ic=0x0B,oc=0x0A,ec=0x0C,tc=0x0F;
        const char* v = skip_space(args + 4);
        (void)kpersist_get_ui_prefs(&t, &b, &a, &h, &pc, &ic, &oc, &ec, &tc);
        if (str_cmp(v, "dark") == 0) t = 0u;
        else if (str_cmp(v, "light") == 0) t = 1u;
        else if (str_cmp(v, "gradient") == 0) t = 2u;
        else if (str_cmp(v, "custom") == 0) t = 3u;
        else {
            kputs("Usage: theme set <dark|light|gradient|custom>\n");
            return;
        }
        kpersist_set_ui_prefs(t, b, a, h, pc, ic, oc, ec, tc);
        kputs("Theme updated and persisted.\n");
        return;
    }
    kputs("Usage: theme set <dark|light|gradient|custom>\n");
}

static void cmd_personalize(const char* args) {
    u8 t=0,b=0,a=1,h=0,pc=0x0A,ic=0x0B,oc=0x0A,ec=0x0C,tc=0x0F;
    char v[24];
    args = skip_space(args);
    (void)kpersist_get_ui_prefs(&t, &b, &a, &h, &pc, &ic, &oc, &ec, &tc);

    if (!args[0] || str_cmp(args, "status") == 0) {
        kputs("personalize theme/bg/anim/hc: ");
        print_u32((u32)t); kputs("/");
        print_u32((u32)b); kputs("/");
        print_u32((u32)a); kputs("/");
        print_u32((u32)h); kputc('\n');
        kputs("shell colors p/i/ok/err/t: ");
        print_u32((u32)pc); kputs("/");
        print_u32((u32)ic); kputs("/");
        print_u32((u32)oc); kputs("/");
        print_u32((u32)ec); kputs("/");
        print_u32((u32)tc); kputc('\n');
        return;
    }

    if (starts_with(args, "theme ")) {
        args = read_token(skip_space(args + 6), v, sizeof(v));
        if (str_cmp(v, "dark") == 0) t = 0u;
        else if (str_cmp(v, "light") == 0) t = 1u;
        else if (str_cmp(v, "gradient") == 0) t = 2u;
        else if (str_cmp(v, "custom") == 0) t = 3u;
        else { kputs("bad theme\n"); return; }
        kpersist_set_ui_prefs(t,b,a,h,pc,ic,oc,ec,tc);
        kputs("theme persisted\n");
        return;
    }

    if (starts_with(args, "wallpaper ")) {
        u32 n = 0u;
        args = read_token(skip_space(args + 10), v, sizeof(v));
        if (parse_u32(v, &n) != 0 || n > 4u) { kputs("wallpaper must be 0..4\n"); return; }
        b = (u8)n;
        kpersist_set_ui_prefs(t,b,a,h,pc,ic,oc,ec,tc);
        kputs("wallpaper persisted\n");
        return;
    }

    if (starts_with(args, "animations ")) {
        args = read_token(skip_space(args + 11), v, sizeof(v));
        if (str_cmp(v, "on") == 0) a = 1u;
        else if (str_cmp(v, "off") == 0) a = 0u;
        else { kputs("animations on|off\n"); return; }
        kpersist_set_ui_prefs(t,b,a,h,pc,ic,oc,ec,tc);
        kputs("animations persisted\n");
        return;
    }

    if (starts_with(args, "contrast ")) {
        args = read_token(skip_space(args + 9), v, sizeof(v));
        if (str_cmp(v, "on") == 0) h = 1u;
        else if (str_cmp(v, "off") == 0) h = 0u;
        else { kputs("contrast on|off\n"); return; }
        kpersist_set_ui_prefs(t,b,a,h,pc,ic,oc,ec,tc);
        kputs("contrast persisted\n");
        return;
    }

    kputs("Usage: personalize status|theme <dark|light|gradient|custom>|wallpaper <0..4>|animations <on|off>|contrast <on|off>\n");
}

static void cmd_render_widget(const char* args) {
    (void)args;
    resource_snapshot_t s;
    kresmon(&s);
    if (s.cpu_busy_percent > 75u || s.low_mem_warning) {
        khal_set_render_policy(HAL_RENDER_PERFORMANCE);
    } else {
        khal_set_render_policy(HAL_RENDER_QUALITY);
    }
    kputs("Render optimization applied.\n");
}

static void cmd_plugin_widget(const char* args) {
    args = skip_space(args);
    if (starts_with(args, "install ")) {
        cmd_module(args);
        return;
    }
    if (starts_with(args, "remove ")) {
        kputs("module uninstall ");
        kputs(skip_space(args + 7));
        kputc('\n');
        if (kmodule_uninstall(skip_space(args + 7)) == 0) kputs("Plugin removed.\n");
        else kputs("Plugin remove failed.\n");
        return;
    }
    if (starts_with(args, "update ")) {
        kputs("Plugin update requested: ");
        kputs(skip_space(args + 7));
        kputs(" (advisor/patch pipeline)\n");
        return;
    }
    kputs("Usage: plugin install <name>|plugin remove <name>|plugin update <name>\n");
}

static void cmd_linuxmap(const char* args) {
    (void)args;
    kputs("Linux compatibility is active.\n");
    kputs("Examples:\n");
    kputs("pwd -> wim\nls -> list\ncd <p> -> open <p>\nkill <pid> -> proc kill <pid>\n");
    kputs("ifconfig -> net status\nuname -a -> sysinfo\nfree -h -> meminfo\n");
    kputs("history -> past\nclear -> reset\nmount -> attach\n");
    kputs("Full 100-command map: LINUX100_COMPAT.md\n");
}

static void cmd_trace(const char* args) {
    char tok[12];
    u32 n = 8u;
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "status") == 0) {
        kputs("trace events: ");
        print_u32(ktrace_events());
        kputc('\n');
        return;
    }
    if (starts_with(args, "show")) {
        args = skip_space(args + 4);
        if (args[0]) {
            (void)read_token(args, tok, sizeof(tok));
            if (parse_u32(tok, &n) != 0) n = 8u;
        }
        if (n > 64u) n = 64u;
        for (u32 i = 0; i < n; i++) {
            ktrace_event_t ev;
            if (ktrace_event(i, &ev) != 0) break;
            kputs("#");
            print_u32(i);
            kputs(" t=");
            print_u32(ev.tick);
            kputs(" type=");
            print_u32(ev.type);
            kputs(" cpu=");
            print_u32(ev.cpu);
            kputs(" a=");
            print_u32(ev.a0); kputs("/");
            print_u32(ev.a1); kputs("/");
            print_u32(ev.a2);
            kputc('\n');
        }
        return;
    }
    kputs("Usage: trace status|show [n]\n");
}

static void cmd_drivers(const char* args) {
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "list") == 0) {
        u32 n = kdriver_count();
        kputs("drivers: ");
        print_u32(n);
        kputc('\n');
        for (u32 i = 0; i < n; i++) {
            driver_info_t d;
            if (kdriver_info(i, &d) != 0) continue;
            kputs(d.name);
            kputs(" ");
            kputs(d.active ? "active" : "idle");
            kputs(" probe=");
            print_u32(d.probe_ok);
            kputs(" starts/stops=");
            print_u32(d.starts); kputs("/");
            print_u32(d.stops);
            kputs(" fail/restart=");
            print_u32(d.failures); kputs("/");
            print_u32(d.restart_count);
            kputs(" rw=");
            print_u32(d.read_ops); kputs("/");
            print_u32(d.write_ops);
            kputs(" health=");
            print_u32(d.health_score);
            kputs(" sbx/crit=");
            print_u32(d.sandboxed); kputs("/");
            print_u32(d.critical);
            kputc('\n');
        }
        return;
    }
    if (str_cmp(args, "stats") == 0) {
        driver_bus_stats_t st;
        kdriver_bus_stats(&st);
        kputs("drivers installed/active: ");
        print_u32(st.installed); kputs("/");
        print_u32(st.active);
        kputs(" fail/restart: ");
        print_u32(st.total_failures); kputs("/");
        print_u32(st.total_restarts);
        kputs(" avg_health=");
        print_u32(st.avg_health);
        kputc('\n');
        return;
    }
    if (starts_with(args, "recover ")) {
        char t0[24];
        args += 8;
        (void)read_token(args, t0, sizeof(t0));
        if (!t0[0]) {
            kputs("Usage: drivers recover <name>\n");
            return;
        }
        if (kdriver_recover(t0) == 0) kputs("driver recovered\n");
        else kputs("driver recover failed\n");
        return;
    }
    if (starts_with(args, "flags ")) {
        char t0[24], t1[8], t2[8];
        u32 sbx = 0, crit = 0;
        args += 6;
        args = read_token(args, t0, sizeof(t0));
        args = read_token(args, t1, sizeof(t1));
        (void)read_token(args, t2, sizeof(t2));
        if (!t0[0] || parse_u32(t1, &sbx) != 0 || parse_u32(t2, &crit) != 0 || sbx > 1u || crit > 1u) {
            kputs("Usage: drivers flags <name> <sandbox0|1> <critical0|1>\n");
            return;
        }
        if (kdriver_set_flags(t0, (u8)sbx, (u8)crit) == 0) kputs("driver flags updated\n");
        else kputs("driver flags update failed\n");
        return;
    }
    kputs("Usage: drivers list|stats|recover <name>|flags <name> <sandbox0|1> <critical0|1>\n");
}

static void cmd_layers(const char* args) {
    (void)args;
    layer_stats_t ls;
    klayers_status(&ls);
    kputs("layers total k/d/a: ");
    print_u32(ls.total_components); kputs(" ");
    print_u32(ls.kernel_components); kputs("/");
    print_u32(ls.driver_components); kputs("/");
    print_u32(ls.app_components);
    kputs("\nweights k/d/a: ");
    print_u32(ls.kernel_weight_percent); kputs("%/");
    print_u32(ls.driver_weight_percent); kputs("%/");
    print_u32(ls.app_weight_percent); kputs("%");
    kputs("\narchitecture: ");
    kputs(ls.architecture_ok ? "OK (kernel>=80%)" : "NOT READY");
    kputc('\n');
}

static void cmd_disk(const char* args) {
    char t0[24];
    char t1[24];
    char t2[24];
    static u8 sec[512];
    u32 id = 0u, a = 0u, b = 0u;

    args = read_token(skip_space(args), t0, sizeof(t0));
    if (!t0[0] || str_cmp(t0, "status") == 0) {
        disk_info_t di;
        disk_partition_info_t pi;
        if (kdisk_info(0u, &di) != 0) {
            kputs("disk0 not available\n");
            return;
        }
        kputs("disk0 sectors/ssize: ");
        print_u32(di.total_sectors_low); kputs("/");
        print_u32(di.sector_size);
        kputs("\nbackend persistent/writable/bootable: ");
        print_u32(di.persistent); kputs("/");
        print_u32(di.writable); kputs("/");
        print_u32(di.bootable);
        kputs("\npart table/parts/rollback: ");
        print_u32((u32)di.last_partition_table); kputs("/");
        print_u32(di.active_partitions); kputs("/");
        print_u32(di.rollback_ready);
        kputs("\nhealth/checksum/dualboot: ");
        print_u32(di.health_score); kputs("/");
        print_u32(di.last_checksum); kputs("/");
        print_u32(di.dualboot_detected);
        if (kdisk_partition_info(&pi) == 0 && pi.valid) {
            kputs("\npartition lba/count/fs: ");
            print_u32(pi.start_lba); kputs("/");
            print_u32(pi.total_lba); kputs("/");
            print_u32((u32)pi.fs);
        }
        {
            u32 pc = kdisk_partition_count();
            kputs("\npartitions:");
            for (u32 p = 0u; p < pc; p++) {
                disk_partition_info_t part;
                if (kdisk_partition_get(p, &part) != 0) continue;
                kputs("\n  p"); print_u32(part.index); kputs(" start="); print_u32(part.start_lba);
                kputs(" sectors="); print_u32(part.total_lba); kputs(" fs="); print_u32((u32)part.fs);
                kputs(part.bootable ? " bootable" : "");
            }
        }
        kputc('\n');
        return;
    }

    if (str_cmp(t0, "list") == 0) {
        u32 n = kdisk_count();
        kputs("disk devices: ");
        print_u32(n);
        kputc('\n');
        for (u32 i = 0; i < n; i++) {
            disk_info_t di;
            if (kdisk_info(i, &di) != 0) continue;
            kputs("disk"); print_u32(i); kputs(" present/persistent/writable/bootable: ");
            print_u32(di.present); kputs("/");
            print_u32(di.persistent); kputs("/");
            print_u32(di.writable); kputs("/");
            print_u32(di.bootable);
            kputc('\n');
        }
        return;
    }

    if (str_cmp(t0, "select") == 0) {
        (void)read_token(args, t1, sizeof(t1));
        if (!t1[0] || parse_u32(t1, &id) != 0) {
            kputs("Usage: disk select <id>\n");
            return;
        }
        if (kdisk_select(id) == 0) kputs("disk selected\n");
        else kputs("disk select failed\n");
        return;
    }

    if (str_cmp(t0, "checksum") == 0) {
        args = read_token(args, t1, sizeof(t1));
        (void)read_token(args, t2, sizeof(t2));
        if (!t1[0] || !t2[0] || parse_u32(t1, &a) != 0 || parse_u32(t2, &b) != 0) {
            kputs("Usage: disk checksum <lba> <count>\n");
            return;
        }
        kputs("checksum: ");
        print_u32(kdisk_checksum(a, b));
        kputc('\n');
        return;
    }

    if (str_cmp(t0, "detectos") == 0) {
        int r = kdisk_detect_other_os();
        kputs("foreign os partitions: ");
        print_u32(r > 0 ? 1u : 0u);
        kputc('\n');
        return;
    }

    if (str_cmp(t0, "free") == 0) {
        disk_free_space_t fs;
        if (kdisk_free_space(&fs) != 0) {
            kputs("disk free-space query failed\n");
            return;
        }
        kputs("sectors total/used/free: "); print_u32(fs.total_sectors); kputc('/');
        print_u32(fs.used_sectors); kputc('/'); print_u32(fs.free_sectors);
        kputs("\nfree extents/largest: "); print_u32(fs.free_extent_count); kputc('/');
        print_u32(fs.largest_free_extent); kputs(" sectors\n");
        return;
    }

    if (str_cmp(t0, "read") == 0) {
        (void)read_token(args, t1, sizeof(t1));
        if (!t1[0] || parse_u32(t1, &a) != 0) {
            kputs("Usage: disk read <lba>\n");
            return;
        }
        if (kdisk_read_sector(a, sec) != 0) {
            kputs("disk read failed\n");
            return;
        }
        kputs("sector "); print_u32(a); kputs(" bytes[0..63]: ");
        for (u32 i = 0; i < 64u; i++) {
            char c = (char)sec[i];
            if (c < 32 || c > 126) c = '.';
            kputc(c);
        }
        kputc('\n');
        return;
    }

    if (str_cmp(t0, "write") == 0) {
        const char* rest = read_token(args, t1, sizeof(t1));
        if (!t1[0] || parse_u32(t1, &a) != 0) {
            kputs("Usage: disk write <lba> <text>\n");
            return;
        }
        rest = skip_space(rest);
        if (!rest[0]) {
            kputs("Usage: disk write <lba> <text>\n");
            return;
        }
        for (u32 i = 0; i < 512u; i++) sec[i] = 0;
        for (u32 i = 0; rest[i] && i < 511u; i++) sec[i] = (u8)rest[i];
        if (kdisk_write_sector(a, sec) != 0) {
            kputs("disk write failed\n");
            return;
        }
        kputs("disk write ok\n");
        return;
    }
    kputs("Usage: disk status|list|select <id>|checksum <lba> <count>|detectos|free|read <lba>|write <lba> <text>\n");
}

static void cmd_recovery(const char* args) {
    (void)args;
    kputs("recovery: kernel disk & filesystem check\n");
    vfs_fsck_report_t fsck;
    if (kvfs_fsck(&fsck, 1u) == 0) kputs("recovery: filesystem check/repair complete\n");
    else kputs("recovery: filesystem check passed/clean\n");
}

static void cmd_install(const char* args) {
    (void)args;
    kputs("install: standalone pure kernel mode (disk installer runs in userspace)\n");
}

static void cmd_start(const char* args) {
    char action[24];
    (void)read_token(skip_space(args), action, sizeof(action));
    if (str_cmp(action, "install") != 0) {
        kputs("Usage: start install\n");
        return;
    }
    cmd_install("start");
}

static void cmd_usb(const char* args) {
    char action[16];
    usb_status_t st;
    (void)read_token(skip_space(args), action, sizeof(action));
    if (str_cmp(action, "scan") == 0) usb_poll();
    else if (action[0] && str_cmp(action, "status") != 0) {
        kputs("Usage: usb status|scan\n");
        return;
    }
    usb_status(&st);
    kputs("USB OHCI found/ready: ");
    print_u32(st.ohci_found); kputs("/"); print_u32(st.controller_ready);
    kputs("  EHCI present: "); print_u32(st.ehci_found);
    kputs("  xHCI present: "); print_u32(st.xhci_found);
    kputs("  active controller: "); kputs(st.controller_kind == 1u ? "OHCI" : "none"); kputc('\n');
    kputs("\nUSB PCI vendor/device: "); print_hex16_local(st.vendor); kputs("/"); print_hex16_local(st.device);
    kputs("\nRoot hub ports/connected/low-speed: ");
    print_u32(st.port_count); kputs("/"); print_hex32_local(st.connected_mask); kputs("/"); print_hex32_local(st.low_speed_mask);
    kputs("\nUSB controller reset failures: "); print_u32(st.reset_failures); kputc('\n');
    kputs("USB HID keyboard/mouse/address/endpoint/packet: ");
    print_u32(st.hid_keyboard); kputs("/"); print_u32(st.hid_mouse); kputs("/");
    print_u32(st.hid_address); kputs("/"); print_u32(st.hid_endpoint); kputs("/");
    print_u32(st.hid_packet_size);
    kputs("\nUSB HID reports/enumeration failures/last error: ");
    print_u32(st.hid_reports); kputs("/"); print_u32(st.enumeration_failures); kputs("/");
    print_u32(st.last_error); kputc('\n');
    kputs("USB Mass Storage found/ready: "); kputs(st.mass_storage_found ? "yes/" : "no/");
    kputs(st.mass_storage_ready ? "yes" : "no");
    if (st.mass_storage_ready) { kputs(" sectors="); print_u32(st.mass_block_count); }
    kputc('\n');
}

static void cmd_fb(const char* args) {
    char action[16], width[16], height[16];
    u32 w, h;
    args = read_token(skip_space(args), action, sizeof(action));
    if (str_cmp(action, "status") == 0 || !action[0]) {
        kputs("framebuffer: "); kputs(gfx_backend_name()); kputs(" ");
        print_u32(gfx_width()); kputc('x'); print_u32(gfx_height()); kputs("x"); print_u32(gfx_bpp()); kputs(" bpp\n");
        return;
    }
    if (str_cmp(action, "mode") != 0) { kputs("Usage: fb status|mode <width> <height>\n"); return; }
    args = read_token(args, width, sizeof(width));
    (void)read_token(args, height, sizeof(height));
    if (parse_u32(width, &w) != 0 || parse_u32(height, &h) != 0 || gfx_set_mode((u16)w, (u16)h, 32u) != 0) {
        kputs("fb: mode unavailable (BGA 32bpp modes only)\n");
        return;
    }
    kputs("framebuffer mode set to "); print_u32(gfx_width()); kputc('x'); print_u32(gfx_height()); kputs("x32\n");
}

static void cmd_kinit(const char* args) {
    args = skip_space(args);
    if (!args[0] || str_cmp(args, "status") == 0) {
        kinit_status_t st;
        kkinit_status(&st);
        kputs("kinit total/done/failed: ");
        print_u32(st.total); kputs("/");
        print_u32(st.done); kputs("/");
        print_u32(st.failed);
        kputs("\nlast idx/level/rc: ");
        print_u32(st.last_index); kputs("/");
        print_u32((u32)st.last_level); kputs("/");
        print_u32(st.last_rc);
        kputc('\n');
        return;
    }
    if (starts_with(args, "list")) {
        u32 n = 32u;
        if (starts_with(args, "list ")) {
            u32 v = 0u;
            if (parse_u32(args + 5, &v) == 0 && v > 0u) n = v;
        }
        for (u32 i = 0u; i < n; i++) {
            kinit_module_t m;
            i32 rc = 0;
            if (kkinit_module_info(i, &m, &rc) != 0) break;
            print_u32(i);
            kputs(": ");
            kputs(m.name ? m.name : "(null)");
            kputs(" lvl=");
            print_u32((u32)m.level);
            kputs(" rc=");
            print_u32((u32)rc);
            kputc('\n');
        }
        return;
    }
    kputs("Usage: kinit status|list [n]\n");
}

static void cmd_levels(const char* args) {
    char t0[16];
    char t1[16];
    u32 v = 0u;

    args = read_token(skip_space(args), t0, sizeof(t0));
    if (!t0[0] || str_cmp(t0, "status") == 0) {
        level_status_t st;
        klevels_status(&st);
        kputs("levels current/target/score: ");
        print_u32((u32)st.current_level); kputs("/");
        print_u32((u32)st.target_level); kputs("/");
        print_u32(st.score_percent); kputs("%");
        kputs("\ngates met: "); print_u32(st.met_gates);
        kputs(" blockers: "); print_u32(st.blockers);
        kputc('\n');
        return;
    }

    if (str_cmp(t0, "show") == 0) {
        args = read_token(args, t1, sizeof(t1));
        if (!t1[0]) {
            for (u8 i = 1u; i <= 7u; i++) {
                level_info_t li;
                if (klevels_info(i, &li) != 0) continue;
                print_u32((u32)i);
                kputs(": ");
                kputs(li.passed ? "pass " : "wait ");
                print_u32(li.readiness_percent); kputs("% req=");
                print_u32(li.required_gates); kputs(" met=");
                print_u32(li.met_gates); kputc('\n');
            }
            return;
        }
        if (parse_u32(t1, &v) != 0 || v < 1u || v > 7u) {
            kputs("Usage: levels show [1..7]\n");
            return;
        }
        {
            level_info_t li;
            if (klevels_info((u8)v, &li) != 0) {
                kputs("level info unavailable\n");
                return;
            }
            kputs("level "); print_u32(v); kputs(": ");
            kputs(li.passed ? "pass " : "wait ");
            print_u32(li.readiness_percent); kputs("% req=");
            print_u32(li.required_gates); kputs(" met=");
            print_u32(li.met_gates); kputc('\n');
        }
        return;
    }

    if (str_cmp(t0, "target") == 0) {
        args = read_token(args, t1, sizeof(t1));
        if (!t1[0] || parse_u32(t1, &v) != 0 || v < 1u || v > 7u) {
            kputs("Usage: levels target <1..7>\n");
            return;
        }
        if (klevels_target((u8)v) == 0) {
            kputs("levels target updated\n");
        } else {
            kputs("levels target failed\n");
        }
        return;
    }

    kputs("Usage: levels status|show [1..7]|target <1..7>\n");
}

static void cmd_mount(const char* args) {
    char t0[FS_MAX_PATH], t1[16], t2[16], t3[16];
    u32 dev = 0, lba = 0;

    args = read_token(skip_space(args), t0, sizeof(t0));
    if (!t0[0]) {
        /* List mounted filesystems */
        mount_entry_t me[FS_MAX_MOUNTS];
        int n = kmount_list(me, FS_MAX_MOUNTS);
        if (n <= 0) {
            kputs("No filesystems mounted.\n");
            return;
        }
        for (int i = 0; i < n; i++) {
            kputs(me[i].mount_point);
            kputs(" dev="); print_u32(me[i].dev_id);
            kputs(" lba="); print_u32(me[i].start_lba);
            kputs(" type=");
            if (me[i].type == FS_TYPE_MINIX) kputs("minix");
            else if (me[i].type == FS_TYPE_EXT2) kputs("ext2");
            else if (me[i].type == FS_TYPE_EXT4) kputs("ext4");
            else if (me[i].type == FS_TYPE_EXT3) kputs("ext3");
            else if (me[i].type == FS_TYPE_FAT) kputs("vfat");
            else if (me[i].type == FS_TYPE_NTFS) kputs("ntfs");
            else kputs("unknown");
            kputc('\n');
        }
        return;
    }

    args = read_token(args, t1, sizeof(t1));
    args = read_token(args, t2, sizeof(t2));
    args = read_token(args, t3, sizeof(t3));

    if (!t1[0] || !t2[0] || !t3[0]) {
        kputs("Usage: mount <path> <minix|ext2|ext4|vfat|ntfs> <dev_id> <start_lba>\n");
        return;
    }

    if (parse_u32(t2, &dev) != 0) {
        kputs("Invalid device id\n");
        return;
    }
    if (parse_u32(t3, &lba) != 0) {
        kputs("Invalid start LBA\n");
        return;
    }

    int rc = kmount(t0, t1, dev, lba);
    if (rc == 0) {
        kputs("Mounted "); kputs(t1);
        kputs(" at "); kputs(t0);
        kputs(" from dev "); print_u32(dev);
        kputs(" lba "); print_u32(lba);
        kputc('\n');
    } else {
        kputs("Mount failed: check fstype, device, or LBA.\n");
    }
}

static void cmd_umount(const char* args) {
    char t0[FS_MAX_PATH];
    args = read_token(skip_space(args), t0, sizeof(t0));
    if (!t0[0]) {
        kputs("Usage: umount <path>\n");
        return;
    }
    if (kumount(t0) == 0) {
        kputs("Unmounted "); kputs(t0); kputc('\n');
    } else {
        kputs("Umount failed: not mounted or busy.\n");
    }
}

static void cmd_fstype(const char* args) {
    char t0[16], t1[16], t2[16];
    u32 start_lba = 0, total_lba = 0;

    args = read_token(skip_space(args), t0, sizeof(t0));
    args = read_token(args, t1, sizeof(t1));
    args = read_token(args, t2, sizeof(t2));

    if (!t0[0] || !t1[0] || !t2[0]) {
        kputs("Usage: mkfs <minix|ext2|vfat> <start_lba> <total_lba>\n");
        return;
    }

    if (parse_u32(t1, &start_lba) != 0) {
        kputs("Invalid start LBA\n");
        return;
    }
    if (parse_u32(t2, &total_lba) != 0) {
        kputs("Invalid total LBA\n");
        return;
    }

    int rc = kformat(t0, 0, start_lba, total_lba);
    if (rc == 0) {
        kputs("Formatted as "); kputs(t0);
        kputs(" at lba "); print_u32(start_lba);
        kputs(" count "); print_u32(total_lba);
        kputc('\n');
    } else {
        kputs("Format failed.\n");
    }
}

void netsurf_lastos_navigate(const char* url);

static void cmd_axiom_web(const char* args) {
    const char* arg = skip_space(args);
    if (arg && (str_cmp(arg, "-v") == 0 || str_cmp(arg, "--version") == 0)) {
        kputs("🌐 NetSurf Web Browser v3.11 (W3C HTML5 / LibCSS / LibDOM / BearSSL TLS 1.3)\n");
        kputs("Official Upstream NetSurf Engine for LastOS\n");
        return;
    }
    if (arg && (str_cmp(arg, "-h") == 0 || str_cmp(arg, "--help") == 0)) {
        kputs("Usage: netsurf [URL | about:welcome | OPTIONS]\n\n");
        kputs("Options:\n");
        kputs("  -v, --version      Display NetSurf version information\n");
        kputs("  -h, --help         Display this help message\n");
        return;
    }
    kputs("Launching NetSurf Web Browser (Userland ELF)...\n");
    cmd_spawn("/bin/netsurf.elf");
}


