/* Filesystem, users, environment, and editor command handlers. */
static void print_two_digits(u8 value) {
    kputc((char)('0' + (value / 10u)));
    kputc((char)('0' + (value % 10u)));
}

static u8 rtc_bcd(u8 value) {
    return (u8)((value & 0x0Fu) + ((value >> 4u) * 10u));
}

static u8 rtc_read(u8 reg) {
    outb(0x70u, reg);
    return rtc_bcd(inb(0x71u));
}

static void cmd_pwd(const char* args) {
    (void)args;
    kputs(commands_cwd());
    kputc('\n');
}

static void cmd_ls(const char* args) {
    char path[128];
    vfs_node_info_t info;
    vfs_node_info_t entries[32];
    args = skip_space(args);
    kvfs_make_path(args, path, sizeof(path));
    if (kvfs_info(path, &info) != 0) {
        kputs("ls: path not found\n");
        return;
    }
    if (info.type == VFS_NODE_FILE) {
        kputs(info.name); kputc('\n');
        return;
    }
    u32 count = kvfs_list(path, entries, (u32)(sizeof(entries) / sizeof(entries[0])));
    for (u32 i = 0u; i < count; i++) {
        kputs(entries[i].type == VFS_NODE_DIR ? "d " : "- ");
        kputs(entries[i].name);
        if (entries[i].type == VFS_NODE_FILE) {
            kputs("  ");
            print_u32(entries[i].size);
        }
        kputc('\n');
    }
}

static void cmd_cd(const char* args) { cmd_open(args); }
static void cmd_cat(const char* args) { cmd_show(args); }
static void cmd_rm(const char* args) { cmd_delete(args); }
static void cmd_mv(const char* args) { cmd_rename(args); }

static void cmd_put(const char* args) {
    char name[VFS_NAME_MAX];
    char path[128];
    vfs_node_info_t info;
    args = read_token(skip_space(args), name, sizeof(name));
    args = skip_space(args);
    if (!name[0] || !args[0]) {
        kputs("Usage: put <file> <text>\n");
        return;
    }
    kvfs_make_path(name, path, sizeof(path));
    if (kvfs_info(path, &info) != 0 && kvfs_create_file(path) != 0) {
        kputs("put: cannot create file\n");
        return;
    }
    if (kvfs_write_file(path, args, str_len(args)) != 0) {
        kputs("put: write failed\n");
        return;
    }
    kputs("saved: "); kputs(path); kputc('\n');
}

static void cmd_whoami(const char* args) {
    char user[24];
    char pass[24];
    (void)args;
    if (kpersist_get_auth(user, sizeof(user), pass, sizeof(pass)) != 0 || !user[0]) {
        kputs("unknown\n");
        return;
    }
    kputs(user);
    kputc('\n');
}

static void cmd_id(const char* args) {
    char user[24];
    char pass[24];
    (void)args;
    if (kpersist_get_auth(user, sizeof(user), pass, sizeof(pass)) != 0 || !user[0]) {
        kputs("uid=0 gid=0\n");
        return;
    }
    kputs("uid=0("); kputs(user); kputs(") gid=0(root) groups=0(root)\n");
}

static void cmd_groups(const char* args) {
    char user[24];
    char pass[24];
    (void)args;
    if (kpersist_get_auth(user, sizeof(user), pass, sizeof(pass)) != 0 || !user[0]) {
        kputs("root\n");
        return;
    }
    kputs("root");
    if (str_cmp(user, "root") != 0) {
        kputc(' ');
        kputs(user);
    }
    kputc('\n');
}

static int require_admin(void) {
    if (userdb_is_admin()) return 1;
    kputs("permission denied: administrator rights required\n");
    return 0;
}

static void cmd_df(const char* args) {
    u32 total = 0u, used = 0u, free_bytes = 0u;
    (void)args;
    if (kvfs_usage(&total, &used, &free_bytes) != 0) { kputs("df: unavailable\n"); return; }
    kputs("Filesystem      Size      Used      Free      Use%\n");
    kputs("lastos-vfs      "); print_u32(total / 1024u); kputs(" KB      ");
    print_u32(used / 1024u); kputs(" KB      "); print_u32(free_bytes / 1024u); kputs(" KB      ");
    print_u32(total ? (used * 100u) / total : 0u); kputs("%\n");
}

static void cmd_fstest(const char* args) {
    static char large[65536];
    vfs_node_info_t info;
    char sample[32];
    args = skip_space(args);
    if (str_cmp(args, "powercut") == 0) {
        static const char marker[] = "WAL power-cut marker\n";
        char verify[32];
        if (kvfs_info("/var", &info) != 0) (void)kvfs_mkdir("/var");
        if (kvfs_info("/var/tmp", &info) != 0) (void)kvfs_mkdir("/var/tmp");
        (void)kvfs_delete("/var/tmp/fs-powercut.marker");
        if (kvfs_create_file("/var/tmp/fs-powercut.marker") != 0 ||
            kvfs_write_file("/var/tmp/fs-powercut.marker", marker, sizeof(marker) - 1u) != 0 ||
            kvfs_flush_disk() != 0 || kpdisk_test_torn_vfs_wal() != 0) {
            kputs("fstest powercut: FAIL (prepare)\n");
            return;
        }
        if (kpdisk_wal_recover() != 0 || kvfs_reload_disk() != 0 ||
            kvfs_read_file("/var/tmp/fs-powercut.marker", verify, sizeof(verify)) <= 0 ||
            str_cmp(verify, marker) != 0) {
            kputs("fstest powercut: FAIL (replay)\n");
            return;
        }
        kputs("fstest powercut: PASS (torn PREPARED WAL replayed safely)\n");
        return;
    }
    if (str_cmp(args, "large") != 0) {
        kputs("Usage: fstest large|powercut\n");
        return;
    }
    if (kvfs_info("/var", &info) != 0) (void)kvfs_mkdir("/var");
    if (kvfs_info("/var/tmp", &info) != 0) (void)kvfs_mkdir("/var/tmp");
    for (u32 i = 0u; i < sizeof(large); i++) large[i] = (char)('A' + (i % 26u));
    (void)kvfs_delete("/var/tmp/fs-large.bin");
    if (kvfs_create_file("/var/tmp/fs-large.bin") != 0 ||
        kvfs_write_file("/var/tmp/fs-large.bin", large, sizeof(large)) != 0 ||
        kvfs_flush_disk() != 0 || kvfs_info("/var/tmp/fs-large.bin", &info) != 0 ||
        info.size != sizeof(large) || kvfs_read_file_at("/var/tmp/fs-large.bin", 32768u, sample, sizeof(sample)) < 16) {
        kputs("fstest large: FAIL\n");
        return;
    }
    for (u32 i = 0u; i < 16u; i++) {
        if (sample[i] != large[32768u + i]) {
            kputs("fstest large: FAIL (content mismatch)\n");
            return;
        }
    }
    kputs("fstest large: PASS 64 KiB persisted and verified\n");
}

static void cmd_fsck(const char* args) {
    vfs_fsck_report_t r;
    u8 repair = 0u;
    args = skip_space(args);
    if (str_cmp(args, "repair") == 0) repair = 1u;
    if (repair && !require_admin()) return;
    kputs("fsck: checking VFS metadata and block references...\n");
    int rc = kvfs_fsck(&r, repair);
    kputs("blocks total/used/referenced: "); print_u32(r.total_blocks); kputs("/");
    print_u32(r.used_blocks); kputs("/"); print_u32(r.referenced_blocks);
    kputs("\norphan blocks: "); print_u32(r.orphan_blocks);
    kputs("\nbad nodes/references: "); print_u32(r.bad_nodes); kputs("/"); print_u32(r.bad_references);
    kputs("\njournal entries: "); print_u32(r.journal_entries); kputc('\n');
    if (rc == 0) kputs("fsck: clean\n");
    else if (repair) kputs("fsck: repaired block bitmap; run fsck again to verify\n");
    else kputs("fsck: errors found; run fsck repair as administrator\n");
}

static void cmd_users(const char* args) {
    (void)args;
    for (u32 i = 0u; i < userdb_count(); i++) {
        user_record_t u;
        if (userdb_get(i, &u) != 0) continue;
        kputs(u.name); kputs(" uid="); print_u32(u.uid); kputs(" gid="); print_u32(u.gid);
        kputs((u.flags & USERDB_FLAG_ADMIN) ? " admin" : " user");
        if (u.flags & USERDB_FLAG_LOCKED) kputs(" locked");
        kputc('\n');
    }
}

static void cmd_adduser(const char* args) {
    char name[USERDB_NAME_MAX], pass[32], role[12];
    if (!require_admin()) return;
    args = read_token(skip_space(args), name, sizeof(name));
    args = read_token(args, pass, sizeof(pass));
    (void)read_token(args, role, sizeof(role));
    if (!name[0] || !pass[0]) { kputs("Usage: adduser <name> <password> [admin]\n"); return; }
    if (userdb_add(name, pass, str_cmp(role, "admin") == 0) != 0) kputs("adduser failed\n");
    else { kputs("user created: "); kputs(name); kputc('\n'); }
}

static void cmd_passwd(const char* args) {
    char name[USERDB_NAME_MAX], pass[32];
    user_record_t current;
    args = read_token(skip_space(args), name, sizeof(name));
    args = read_token(args, pass, sizeof(pass));
    if (!name[0] || !pass[0]) { kputs("Usage: passwd <user> <new-password>\n"); return; }
    if (!userdb_current(&current) || (str_cmp(name, current.name) == 0 && !userdb_is_admin())) {
        if (userdb_change_password(name, pass) != 0) kputs("passwd failed\n");
        else kputs("password updated\n");
        return;
    }
    if (!userdb_is_admin()) { kputs("passwd: permission denied\n"); return; }
    if (userdb_change_password(name, pass) != 0) kputs("passwd failed\n");
    else kputs("password updated\n");
}

static void cmd_usermod(const char* args) {
    char action[12], name[USERDB_NAME_MAX];
    if (!require_admin()) return;
    args = read_token(skip_space(args), action, sizeof(action));
    (void)read_token(args, name, sizeof(name));
    if (!name[0] || (str_cmp(action, "lock") != 0 && str_cmp(action, "unlock") != 0)) {
        kputs("Usage: usermod <lock|unlock> <user>\n"); return;
    }
    if (userdb_set_locked(name, str_cmp(action, "lock") == 0) != 0) kputs("usermod failed\n");
    else kputs("account status updated\n");
}

static void cmd_admin(const char* args) {
    u32 uid, gid;
    args = skip_space(args);
    if (!args[0]) { kputs("Usage: admin <command>\n"); return; }
    if (!require_admin()) return;
    uid = userdb_current_uid();
    gid = userdb_current_gid();
    kvfs_set_credentials(0u, 0u);
    (void)commands_execute_status(args);
    kvfs_set_credentials(uid, gid);
}

static void cmd_env(const char* args) {
    (void)args;
    for (u32 i = 0u; i < cmd_env_count; i++) {
        kputs(cmd_env_names[i]); kputc('='); kputs(cmd_env_values[i]); kputc('\n');
    }
}

static void cmd_setenv(const char* args) {
    char name[24];
    args = read_token(skip_space(args), name, sizeof(name));
    args = skip_space(args);
    if (!name[0] || !args[0] || env_set_value(name, args) != 0) {
        kputs("Usage: setenv <name> <value>\n");
        return;
    }
    kputs("environment updated\n");
}

static void cmd_unsetenv(const char* args) {
    char name[24];
    (void)read_token(skip_space(args), name, sizeof(name));
    if (!name[0] || !env_name_valid(name)) {
        kputs("Usage: unsetenv <name>\n");
        return;
    }
    env_remove_value(name);
}

static void cmd_run(const char* args) {
    char name[96];
    char path[128];
    char script[8192];
    args = read_token(skip_space(args), name, sizeof(name));
    if (!name[0]) { kputs("Usage: run <file.ax>\n"); return; }
    u32 name_len = str_len(name);
    if (name_len < 3u || str_cmp(name + name_len - 3u, ".ax") != 0) {
        kputs("run: only .ax scripts are allowed\n");
        return;
    }
    kvfs_make_path(name, path, sizeof(path));
    int n = kvfs_read_file(path, script, sizeof(script) - 1u);
    if (n <= 0) { kputs("run: script not found or empty\n"); return; }
    script[n] = '\0';
    u32 start = 0u;
    for (u32 i = 0u; i <= (u32)n; i++) {
        if (script[i] == '\n' || script[i] == '\0') {
            char line[192];
            char expanded[192];
            shell_trim_copy(script + start, i - start, line, sizeof(line));
            if (line[0] && line[0] != '#') {
                (void)commands_env_expand(line, expanded, sizeof(expanded));
                (void)commands_execute_status(expanded);
            }
            start = i + 1u;
        }
    }
}

static void cmd_date(const char* args) {
    u8 second, minute, hour, day, month, year;
    (void)args;
    second = rtc_read(0x00u);
    minute = rtc_read(0x02u);
    hour = rtc_read(0x04u);
    day = rtc_read(0x07u);
    month = rtc_read(0x08u);
    year = rtc_read(0x09u);
    kputs("20"); print_two_digits(year); kputc('-'); print_two_digits(month);
    kputc('-'); print_two_digits(day); kputc(' '); print_two_digits(hour);
    kputc(':'); print_two_digits(minute); kputc(':'); print_two_digits(second);
    kputc('\n');
}

static void cmd_cp(const char* args) {
    char source[96];
    char destination[96];
    char source_path[96];
    char destination_path[96];
    char data[VFS_FILE_MAX];
    vfs_node_info_t destination_info;
    int n;
    args = read_token(skip_space(args), source, sizeof(source));
    args = read_token(args, destination, sizeof(destination));
    if (!source[0] || !destination[0]) {
        kputs("Usage: cp <source> <destination>\n");
        return;
    }
    kvfs_make_path(source, source_path, sizeof(source_path));
    kvfs_make_path(destination, destination_path, sizeof(destination_path));
    n = kvfs_read_file(source_path, data, sizeof(data));
    if (n < 0) {
        kputs("cp: source file not found\n");
        return;
    }
    if (kvfs_create_file(destination_path) != 0 && kvfs_info(destination_path, &destination_info) != 0) {
        kputs("cp: cannot create destination\n");
        return;
    }
    if (kvfs_write_file(destination_path, data, (u32)n) != 0) {
        kputs("cp: write failed\n");
        return;
    }
    kputs("copied "); kputs(source); kputs(" -> "); kputs(destination); kputc('\n');
}

static void cmd_wim(const char* args) {
    vfs_node_info_t entries[32];
    u32 count;
    (void)args;
    kputs(kvfs_cwd);
    kputc('\n');
    count = kvfs_list(kvfs_cwd, entries, (u32)(sizeof(entries) / sizeof(entries[0])));
    for (u32 i = 0; i < count; i++) {
        kputs(entries[i].type == VFS_NODE_DIR ? "[D] " : "[F] ");
        kputs(entries[i].name);
        if (entries[i].type == VFS_NODE_FILE) {
            kputs(" (");
            print_u32(entries[i].size);
            kputs(")");
        }
        kputc('\n');
    }
}

static void cmd_open(const char* args) {
    char path[128];
    vfs_node_info_t info;
    args = skip_space(args);
    if (!args[0]) {
        cmd_wim("");
        return;
    }
    kvfs_make_path(args, path, sizeof(path));
    if (kvfs_info(path, &info) != 0 || info.type != VFS_NODE_DIR) {
        kputs("Path not found.\n");
        return;
    }
    str_ncpy(kvfs_cwd, path, sizeof(kvfs_cwd));
    kvfs_cwd[sizeof(kvfs_cwd) - 1u] = '\0';
    kputs("cwd: ");
    kputs(kvfs_cwd);
    kputc('\n');
}

static void cmd_create(const char* args) {
    char t1[16];
    char name[VFS_NAME_MAX];
    char path[128];
    i32 rc;
    args = skip_space(args);
    if (!args[0]) {
        kputs("Usage: create <name>\n");
        return;
    }
    args = read_token(args, t1, sizeof(t1));
    if (str_cmp(t1, "file") == 0 || str_cmp(t1, "folder") == 0) {
        (void)read_token(args, name, sizeof(name));
        if (!name[0]) {
            kputs("Usage: create file <name> | create folder <name>\n");
            return;
        }
        kvfs_join_name(name, path, sizeof(path));
        rc = (str_cmp(t1, "folder") == 0) ? kvfs_mkdir(path) : kvfs_create_file(path);
    } else {
        str_ncpy(name, t1, sizeof(name));
        kvfs_join_name(name, path, sizeof(path));
        rc = kvfs_create_file(path);
    }
    if (rc != 0) {
        kputs("Create failed.\n");
    } else {
        kputs("Created: ");
        kputs(name);
        kputc('\n');
    }
}

static void cmd_vfsctl(const char* args) {
    char t0[16], t1[64], t2[16], t3[16];
    u32 uid = 0, gid = 0, mode = 0, n = 8;
    args = skip_space(args);

    if (!args[0] || str_cmp(args, "stats") == 0) {
        vfs_stats_t st;
        kvfs_stats(&st);
        kputs("vfs nodes active/dirs/files: ");
        print_u32(st.active_nodes); kputs("/");
        print_u32(st.dirs); kputs("/");
        print_u32(st.files);
        kputs("\njournal count: ");
        print_u32(kvfs_journal_count());
        kputs("\npersistent backend: ");
        kputs(kpersistent_disk_ready() ? "ready" : "unavailable (RAM/volatile)");
        kputs("\nflush status: ");
        print_u32(kvfs_flush_disk() == 0 ? 1u : 0u);
        kputc('\n');
        return;
    }

    if (str_cmp(args, "flush") == 0) {
        if (kvfs_flush_disk() == 0) {
            kputs("vfs flush: ok\n");
        } else {
            kputs("vfs flush: failed (no persistent disk backend)\n");
        }
        return;
    }

    if (starts_with(args, "cred ")) {
        args += 5;
        args = read_token(args, t0, sizeof(t0));
        (void)read_token(args, t1, sizeof(t1));
        if (parse_u32(t0, &uid) != 0 || parse_u32(t1, &gid) != 0) {
            kputs("Usage: vfsctl cred <uid> <gid>\n");
            return;
        }
        kvfs_set_credentials(uid, gid);
        kputs("vfs credentials updated.\n");
        return;
    }

    if (starts_with(args, "chmod ")) {
        args += 6;
        args = read_token(args, t1, sizeof(t1));
        (void)read_token(args, t2, sizeof(t2));
        if (!t1[0] || parse_u32(t2, &mode) != 0) {
            kputs("Usage: vfsctl chmod <path> <mode>\n");
            return;
        }
        if (kvfs_set_permissions(t1, (u16)(mode & 0777u)) != 0) kputs("chmod failed\n");
        else kputs("chmod ok\n");
        return;
    }

    if (starts_with(args, "chown ")) {
        args += 6;
        args = read_token(args, t1, sizeof(t1));
        args = read_token(args, t2, sizeof(t2));
        (void)read_token(args, t3, sizeof(t3));
        if (!t1[0] || parse_u32(t2, &uid) != 0 || parse_u32(t3, &gid) != 0) {
            kputs("Usage: vfsctl chown <path> <uid> <gid>\n");
            return;
        }
        if (kvfs_set_owner(t1, uid, gid) != 0) kputs("chown failed\n");
        else kputs("chown ok\n");
        return;
    }

    if (starts_with(args, "lock ")) {
        args += 5;
        args = read_token(args, t1, sizeof(t1));
        (void)read_token(args, t2, sizeof(t2));
        if (!t1[0] || parse_u32(t2, &mode) != 0 || mode > 1u) {
            kputs("Usage: vfsctl lock <path> <0|1>\n");
            return;
        }
        if (kvfs_set_lock(t1, (u8)mode) != 0) kputs("lock failed\n");
        else kputs("lock updated\n");
        return;
    }

    if (starts_with(args, "access ")) {
        u8 mask = 0u;
        args += 7;
        args = read_token(args, t1, sizeof(t1));
        (void)read_token(args, t2, sizeof(t2));
        if (!t1[0] || !t2[0]) {
            kputs("Usage: vfsctl access <path> <r|w|x|rw|rx|wx|rwx>\n");
            return;
        }
        for (u32 i = 0; t2[i]; i++) {
            if (t2[i] == 'r') mask |= VFS_ACC_READ;
            if (t2[i] == 'w') mask |= VFS_ACC_WRITE;
            if (t2[i] == 'x') mask |= VFS_ACC_EXEC;
        }
        if (mask == 0u) {
            kputs("Usage: vfsctl access <path> <r|w|x|rw|rx|wx|rwx>\n");
            return;
        }
        if (kvfs_access(t1, mask) == 0) kputs("access: allowed\n");
        else kputs("access: denied\n");
        return;
    }

    if (starts_with(args, "journal")) {
        args += 7;
        args = skip_space(args);
        if (args[0]) {
            (void)read_token(args, t0, sizeof(t0));
            if (parse_u32(t0, &n) != 0) n = 8u;
        }
        if (n > 32u) n = 32u;
        for (u32 i = 0; i < n; i++) {
            vfs_journal_entry_t e;
            if (kvfs_journal_get(i, &e) != 0) break;
            kputs("#");
            print_u32(e.id);
            kputs(" ev=");
            print_u32((u32)e.event);
            kputs(" uid=");
            print_u32(e.actor_uid);
            kputs(" node=");
            print_u32(e.node_id);
            kputs(" p=");
            print_u32(e.parent_id);
            kputs(" a=");
            print_u32(e.arg0); kputs("/");
            print_u32(e.arg1);
            kputs(" name=");
            kputs(e.name);
            kputc('\n');
        }
        return;
    }

    kputs("Usage: vfsctl stats|flush|cred <uid> <gid>|chmod <path> <mode>|chown <path> <uid> <gid>|lock <path> <0|1>|access <path> <r|w|x|rw|rx|wx|rwx>|journal [n]\n");
}

static void cmd_edit(const char* args) {
    char name[VFS_NAME_MAX];
    char path[128];
    char data[VFS_FILE_MAX];
    vfs_node_info_t info;
    u32 len = 0;
    args = skip_space(args);
    if (!args[0]) {
        kputs("Usage: edit <name>\n");
        return;
    }
    (void)read_token(args, name, sizeof(name));
    if (!name[0]) {
        kputs("Usage: edit <name>\n");
        return;
    }
    kvfs_join_name(name, path, sizeof(path));
    if (kvfs_info(path, &info) != 0) {
        if (kvfs_create_file(path) != 0) {
            kputs("Cannot create file.\n");
            return;
        }
    } else if (info.type != VFS_NODE_FILE) {
        kputs("Not a file.\n");
        return;
    }
    if (kvfs_read_file(path, data, sizeof(data)) > 0) {
        len = str_len(data);
    } else {
        data[0] = '\0';
        len = 0u;
    }
    kputs("Write mode: ESC=save, Backspace=delete, Enter=newline\n");
    if (len > 0) {
        kputs("Current:\n");
        kputs(data);
        kputc('\n');
    }
    for (;;) {
        int ch = kgetc();
        if (ch == 27) {
            data[len] = '\0';
            if (kvfs_write_file(path, data, len) == 0) {
                kputs("\nSaved.\n");
            } else {
                kputs("\nSave failed.\n");
            }
            break;
        }
        if (ch == '\b') {
            if (len > 0) {
                len--;
                kputc('\b');
            }
            continue;
        }
        if (ch == '\n') {
            if (len + 1u < sizeof(data)) {
                data[len++] = '\n';
                kputc('\n');
            }
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            if (len + 1u < sizeof(data)) {
                data[len++] = (char)ch;
                kputc((char)ch);
            }
        }
    }
}

static void cmd_nano(const char* args) {
    char name[VFS_NAME_MAX];
    char path[128];
    char data[VFS_FILE_MAX];
    vfs_node_info_t info;
    u32 len = 0u;
    u8 dirty = 0u;
    args = skip_space(args);
    if (!args[0]) {
        kputs("Usage: nano <file>\n");
        return;
    }
    (void)read_token(args, name, sizeof(name));
    if (!name[0]) {
        kputs("Usage: nano <file>\n");
        return;
    }
    kvfs_join_name(name, path, sizeof(path));
    if (kvfs_info(path, &info) != 0) {
        if (kvfs_create_file(path) != 0) {
            kputs("nano: cannot create file\n");
            return;
        }
    } else if (info.type != VFS_NODE_FILE) {
        kputs("nano: not a file\n");
        return;
    }
    if (kvfs_read_file(path, data, sizeof(data)) > 0) {
        len = str_len(data);
    } else {
        data[0] = '\0';
    }

    kputs("GNU nano-like LastOS editor\n");
    kputs("^S Save  ^X Exit  ^C Cancel  Backspace Delete  Enter Newline\n");
    kputs("File: ");
    kputs(path);
    kputs("\n----\n");
    if (len) kputs(data);
    kputs("\n----\n");

    for (;;) {
        int ch = kgetc();
        if (ch == 19) {
            data[len] = '\0';
            if (kvfs_write_file(path, data, len) == 0 && kvfs_flush_disk() == 0) {
                dirty = 0u;
                kputs("\n[nano] saved to persistent disk\n");
            } else {
                kputs("\n[nano] save failed: persistent disk not ready\n");
            }
            continue;
        }
        if (ch == 24 || ch == 27) {
            if (dirty) {
                data[len] = '\0';
                if (kvfs_write_file(path, data, len) != 0 || kvfs_flush_disk() != 0) {
                    kputs("\n[nano] exit blocked: save failed\n");
                    continue;
                }
            }
            kputs("\n[nano] exit\n");
            break;
        }
        if (ch == 3) {
            kputs("\n[nano] canceled\n");
            break;
        }
        if (ch == '\b') {
            if (len > 0u) {
                len--;
                data[len] = '\0';
                dirty = 1u;
                kputc('\b');
            }
            continue;
        }
        if (ch == '\n') {
            if (len + 1u < sizeof(data)) {
                data[len++] = '\n';
                data[len] = '\0';
                dirty = 1u;
                kputc('\n');
            } else {
                kputs("\n[nano] file full\n");
            }
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            if (len + 1u < sizeof(data)) {
                data[len++] = (char)ch;
                data[len] = '\0';
                dirty = 1u;
                kputc((char)ch);
            } else {
                kputs("\n[nano] file full\n");
            }
        }
    }
}

static void cmd_write(const char* args) {
    cmd_edit(args);
}

static void cmd_show(const char* args) {
    char name[VFS_NAME_MAX];
    char path[128];
    char data[VFS_FILE_MAX];
    vfs_node_info_t info;
    u32 offset = 0u;
    args = skip_space(args);
    if (!args[0]) {
        kputs("Usage: show <name>\n");
        return;
    }
    (void)read_token(args, name, sizeof(name));
    kvfs_join_name(name, path, sizeof(path));
    if (kvfs_info(path, &info) != 0 || info.type != VFS_NODE_FILE) {
        kputs("File not found.\n");
        return;
    }
    if (info.size == 0u) {
        kputs("(empty)\n");
        return;
    }
    while (offset < info.size) {
        int n = kvfs_read_file_at(path, offset, data, sizeof(data));
        if (n <= 0) break;
        for (int i = 0; i < n; i++) kputc(data[i]);
        offset += (u32)n;
    }
    kputc('\n');
}

static void cmd_delete(const char* args) {
    char name[VFS_NAME_MAX];
    char path[128];
    args = skip_space(args);
    if (!args[0]) {
        kputs("Usage: delete <name>\n");
        return;
    }
    (void)read_token(args, name, sizeof(name));
    kvfs_join_name(name, path, sizeof(path));
    if (kvfs_delete(path) != 0) {
        kputs("Not found.\n");
        return;
    }
    kputs("Deleted.\n");
}

static void cmd_rename(const char* args) {
    char oldn[VFS_NAME_MAX];
    char newn[VFS_NAME_MAX];
    char path[128];
    args = read_token(skip_space(args), oldn, sizeof(oldn));
    (void)read_token(args, newn, sizeof(newn));
    if (!oldn[0] || !newn[0]) {
        kputs("Usage: rename <old> <new>\n");
        return;
    }
    if (str_len(newn) >= VFS_NAME_MAX) {
        kputs("New name too long.\n");
        return;
    }
    kvfs_join_name(oldn, path, sizeof(path));
    if (kvfs_rename(path, newn) != 0) {
        kputs("Not found.\n");
        return;
    }
    kputs("Renamed.\n");
}

static void cmd_touch(const char* args) {
    char b[64];
    str_ncpy(b, "file ", sizeof(b));
    safe_append(b, sizeof(b), skip_space(args));
    cmd_create(b);
}

static void cmd_mkdir(const char* args) {
    char b[64];
    str_ncpy(b, "folder ", sizeof(b));
    safe_append(b, sizeof(b), skip_space(args));
    cmd_create(b);
}

static void cmd_rmdir(const char* args) {
    char name[VFS_NAME_MAX];
    char path[128];
    args = skip_space(args);
    if (!args[0]) {
        kputs("Usage: rmdir <name>\n");
        return;
    }
    (void)read_token(args, name, sizeof(name));
    kvfs_join_name(name, path, sizeof(path));
    if (kvfs_delete(path) != 0) {
        kputs("Folder not found.\n");
        return;
    }
    kputs("Folder removed.\n");
}
