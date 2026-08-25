#include "commands.h"
#include "kernel_api.h"
#include "string.h"
#include "types.h"
#include "shell_threatlog.h"
#include "shell_processinfo.h"
#include "shell_sysinfo.h"
#include "ip.h"
#include "dns.h"
#include "io.h"
#include "pci.h"
#include "nic.h"
#include "ipv6.h"
#include "usb.h"
#include "gfx.h"
#include "virtio_blk.h"
#include "virtio_net.h"
#include "tls.h"
#include "httpd.h"
#include "userdb.h"
#include "syscall.h"
#include "fde.h"
#include "netstack.h"
#include "self_heal.h"
#include "airmesh.h"
#include "linux_compat.h"

typedef void (*cmd_fn)(const char* args);

static void cmd_linux(const char* args);

static void cmd_axiom_web(const char* args);

typedef struct {
    const char* name;
    const char* desc;
    const char* usage;
    const char* module;
    cmd_fn fn;
} cmd_def_t;

typedef struct {
    u16 port;
    u8 used;
    u8 allow;
} fw_rule_t;

static void cmd_list(const char* args);
static void cmd_help(const char* args);
static void cmd_past(const char* args);
static void cmd_clear(const char* args);
static void cmd_ticks(const char* args);
static void cmd_pwd(const char* args);
static void cmd_ls(const char* args);
static void cmd_cd(const char* args);
static void cmd_cat(const char* args);
static void cmd_rm(const char* args);
static void cmd_mv(const char* args);
static void cmd_put(const char* args);
static void cmd_whoami(const char* args);
static void cmd_id(const char* args);
static void cmd_groups(const char* args);
static void cmd_users(const char* args);
static void cmd_adduser(const char* args);
static void cmd_passwd(const char* args);
static void cmd_usermod(const char* args);
static void cmd_admin(const char* args);
static void cmd_env(const char* args);
static void cmd_setenv(const char* args);
static void cmd_unsetenv(const char* args);
static void cmd_run(const char* args);
static void cmd_date(const char* args);
static void cmd_cp(const char* args);
static void cmd_systeminfo(const char* args);
static void cmd_ifconfig(const char* args);
static void cmd_ping6(const char* args);
static void cmd_status(const char* args);
static void cmd_reboot(const char* args);
static void cmd_shutdown(const char* args);
static void cmd_fde_format(const char* args);
static void cmd_fde_unlock(const char* args);
static void cmd_fde_lock(const char* args);
static void cmd_fde_status(const char* args);
static void cmd_alloc(const char* args);
static void cmd_free(const char* args);
static void cmd_panic(const char* args);
static void cmd_echo(const char* args);
static void cmd_sysinfo(const char* args);
static void cmd_cpuinfo(const char* args);
static void cmd_smp(const char* args);
static void cmd_meminfo(const char* args);
static void cmd_vmstat(const char* args);
static void cmd_memlab(const char* args);
static void cmd_save(const char* args);
static void cmd_load(const char* args);
static void cmd_bootcount(const char* args);
static void cmd_copy(const char* args);
static void cmd_paste(const char* args);
static void cmd_patchtest(const char* args);
static void cmd_selftest(const char* args);
static void cmd_betatest(const char* args);
static void cmd_watchdog(const char* args);
static void cmd_paniclog(const char* args);
static void cmd_antivirus(const char* args);
static void cmd_threatlog(const char* args);
static void cmd_processinfo(const char* args);
static void cmd_ps(const char* args);
static void cmd_kill(const char* args);
static void cmd_exec(const char* args);
static void cmd_spawn(const char* args);
static void cmd_usertest(const char* args);
static void cmd_gfxdemo(const char* args);
static void cmd_gui(const char* args);
static void cmd_grep(const char* args);
static void cmd_procctl(const char* args);
static void cmd_lastmap(const char* args);
static void cmd_lastscan(const char* args);
static void cmd_lastshark(const char* args);
static void cmd_firewall(const char* args);
static void cmd_module(const char* args);
static void cmd_wim(const char* args);
static void cmd_open(const char* args);
static void cmd_create(const char* args);
static void cmd_vfsctl(const char* args);
static void cmd_df(const char* args);
static void cmd_fsck(const char* args);
static void cmd_fstest(const char* args);
static int require_admin(void);
static void cmd_edit(const char* args);
static void cmd_nano(const char* args);
static void cmd_write(const char* args);
static void cmd_show(const char* args);
static void cmd_delete(const char* args);
static void cmd_rename(const char* args);
static void cmd_touch(const char* args);
static void cmd_mkdir(const char* args);
static void cmd_rmdir(const char* args);
static void cmd_resmon(const char* args);
static void cmd_rescheck(const char* args);
static void cmd_mode(const char* args);
static void cmd_cgroup(const char* args);
static void cmd_ns(const char* args);
static void cmd_sysfilter(const char* args);
static void cmd_ipc(const char* args);
static void cmd_sched(const char* args);
static void cmd_schedlab(const char* args);
static void cmd_iolab(const char* args);
static void cmd_seclab(const char* args);
static void cmd_compatlab(const char* args);
static void cmd_driverlab(const char* args);
static void cmd_fusionlab(const char* args);
static void cmd_profile(const char* args);
static void cmd_sandbox(const char* args);
static void cmd_sdc(const char* args);
static void cmd_kshield(const char* args);
static void cmd_secmem(const char* args);
static void cmd_hal(const char* args);
static void cmd_ultimate(const char* args);
static void cmd_kcore(const char* args);
static void cmd_pcoe(const char* args);
static void cmd_compat(const char* args);
static void cmd_sys_widget(const char* args);
static void cmd_proc_widget(const char* args);
static void cmd_net_widget(const char* args);
static void cmd_netstack(const char* args);
static void cmd_audio(const char* args);
static void cmd_dns(const char* args);
static void cmd_vpn(const char* args);
static void cmd_ping(const char* args);
static void cmd_arp(const char* args);
static void cmd_curl(const char* args);
static void cmd_wget(const char* args);
static void cmd_dig(const char* args);
static void cmd_whois(const char* args);
static void cmd_dns_server(const char* args);
static void cmd_dhcp_server(const char* args);
static void cmd_nat(const char* args);
static void cmd_traceroute(const char* args);
static void cmd_netconsole(const char* args);
static void cmd_tlsserver(const char* args);
static void cmd_httpd(const char* args);
static void cmd_av_widget(const char* args);
static void cmd_fw_widget(const char* args);
static void cmd_logs_widget(const char* args);
static void cmd_storage_widget(const char* args);
static void cmd_vps_widget(const char* args);
static void cmd_predict_widget(const char* args);
static void cmd_theme_widget(const char* args);
static void cmd_personalize(const char* args);
static void cmd_render_widget(const char* args);
static void cmd_plugin_widget(const char* args);
static void cmd_linuxmap(const char* args);
static void cmd_trace(const char* args);
static void cmd_drivers(const char* args);
static void cmd_layers(const char* args);
static void cmd_disk(const char* args);
static void cmd_install(const char* args);
static void cmd_recovery(const char* args);
static void cmd_start(const char* args);
static void cmd_usb(const char* args);
static void cmd_fb(const char* args);
static void cmd_kinit(const char* args);
static void cmd_levels(const char* args);
static void cmd_mount(const char* args);
static void cmd_umount(const char* args);
static void cmd_fstype(const char* args);
static void cmd_airmesh(const char* args);
static void cmd_neural(const char* args);
static void cmd_ghost(const char* args);
static void print_u32(u32 value);
static void print_u64(u64 value);
static const char* skip_space(const char* s);
static void safe_append(char* out, u32 out_sz, const char* text);

static int shell_tls_print_body(const u8* data, u32 len, void* context) {
    (void)context;
    if (!data) return -1;
    for (u32 i = 0u; i < len; i++) kputc((char)data[i]);
    return 0;
}

static void ai_assist_hint(const char* topic, const resource_snapshot_t* s) {
    if (!topic || !s) {
        return;
    }
    if (str_cmp(topic, "lastmap") == 0 && s->cpu_busy_percent > 70) {
        kputs("AI: LastMap heavy scan detected. Suggestion: allocate ");
        print_u32(s->recommended_threads);
        kputs(" threads, enable predictive memory caching ");
        print_u32((s->predicted_next_alloc / 1024u) + 512u);
        kputs("KB.\n");
    } else if (s->low_mem_warning) {
        kputs("AI: Low memory pressure detected. Suggestion: switch profile to desktop-light or server-light.\n");
    }
}

static const cmd_def_t cmds[] = {
    { "list", "Show available commands", "list", 0, cmd_list },
    { "help", "Inline help for command", "help [command]", 0, cmd_help },
    { "past", "Show recent command history", "past", 0, cmd_past },
    { "history", "Alias of past", "history", 0, cmd_past },
    { "clear", "Clear VGA screen", "clear", 0, cmd_clear },
    { "echo", "Print text", "echo <text>", 0, cmd_echo },
    { "ticks", "Show timer heartbeat", "ticks", 0, cmd_ticks },
    { "pwd", "Print current directory", "pwd", 0, cmd_pwd },
    { "ls", "List files and folders", "ls [path]", 0, cmd_ls },
    { "cd", "Change current directory", "cd <path>", 0, cmd_cd },
    { "cat", "Print file content", "cat <file>", 0, cmd_cat },
    { "rm", "Remove a file or empty folder", "rm <path>", 0, cmd_rm },
    { "mv", "Rename a file or folder", "mv <old> <new-name>", 0, cmd_mv },
    { "put", "Write text to a persistent file", "put <file> <text>", 0, cmd_put },
    { "whoami", "Print current user", "whoami", 0, cmd_whoami },
    { "id", "Print current user and group IDs", "id", 0, cmd_id },
    { "groups", "Print current user groups", "groups", 0, cmd_groups },
    { "users", "List local users", "users", 0, cmd_users },
    { "adduser", "Create a normal or admin user", "adduser <name> <password> [admin]", 0, cmd_adduser },
    { "passwd", "Change a user password", "passwd <user> <new-password>", 0, cmd_passwd },
    { "usermod", "Lock or unlock a user account", "usermod <lock|unlock> <user>", 0, cmd_usermod },
    { "admin", "Run a command with local administrator credentials", "admin <command>", 0, cmd_admin },
    { "env", "Show environment variables", "env", 0, cmd_env },
    { "setenv", "Set an environment variable", "setenv <name> <value>", 0, cmd_setenv },
    { "unsetenv", "Remove an environment variable", "unsetenv <name>", 0, cmd_unsetenv },
    { "run", "Run a persistent .ax script", "run <file.ax>", 0, cmd_run },
    { "date", "Read the hardware RTC clock", "date", 0, cmd_date },
    { "status", "Show kernel status", "status", 0, cmd_status },
    { "sysinfo", "Show kernel resource metrics", "sysinfo", 0, cmd_sysinfo },
    { "systeminfo", "Detect hardware and system configuration", "systeminfo", 0, cmd_systeminfo },
    { "ifconfig", "Show network interface configuration", "ifconfig", 0, cmd_ifconfig },
    { "ping6", "Send ICMPv6 echo requests", "ping6 <ipv6-address>", 0, cmd_ping6 },
    { "cpuinfo", "Show detected CPU capabilities", "cpuinfo", 0, cmd_cpuinfo },
    { "smp", "Show real symmetric multiprocessing status", "smp status", 0, cmd_smp },
    { "meminfo", "Show memory and allocator metrics", "meminfo [diag]", 0, cmd_meminfo },
    { "vmstat", "Show VM/COW counters", "vmstat", 0, cmd_vmstat },
    { "memlab", "Memory lab diagnostics and stress", "memlab status|validate|reset|run <scenario 0..4> [rounds] [payload]|hist [bins]", 0, cmd_memlab },
    { "alloc", "Allocate heap block", "alloc <bytes>", 0, cmd_alloc },
    { "free", "Free last allocated block", "free", 0, cmd_free },
    { "panic", "Trigger kernel panic", "panic [message]", 0, cmd_panic },
    { "save", "Persist text across reboot", "save <text>", 0, cmd_save },
    { "load", "Load persisted text", "load", 0, cmd_load },
    { "bootcount", "Show persisted boot counter", "bootcount", 0, cmd_bootcount },
    { "reboot", "Reboot the system", "reboot", 0, cmd_reboot },
    { "shutdown", "Shut down the system", "shutdown", 0, cmd_shutdown },
    { "fde-format", "Format partition with Full Disk Encryption", "fde-format <start_lba> <total_lba>", 0, cmd_fde_format },
    { "fde-unlock", "Unlock FDE-encrypted partition", "fde-unlock <start_lba>", 0, cmd_fde_unlock },
    { "fde-lock", "Lock FDE and clear keys from memory", "fde-lock", 0, cmd_fde_lock },
    { "fde-status", "Show FDE encryption status", "fde-status", 0, cmd_fde_status },
    { "copy", "Copy text to clipboard", "copy <text>", 0, cmd_copy },
    { "cp", "Copy a VFS file", "cp <source> <destination>", 0, cmd_cp },
    { "paste", "Print clipboard text", "paste", 0, cmd_paste },
    { "patchtest", "Run secure patch self-test", "patchtest", 0, cmd_patchtest },
    { "selftest", "Run kernel regression tests", "selftest full|status|stress [rounds]|fuzz [rounds]", 0, cmd_selftest },
    { "betatest", "Run non-destructive beta readiness checks", "betatest", 0, cmd_betatest },
    { "watchdog", "Watchdog status and policy", "watchdog status|policy|set <stall_ticks> <oom_spike> <heap_period> <auto0|1>", 0, cmd_watchdog },
    { "paniclog", "Kernel panic history", "paniclog status|show [n]|clear", 0, cmd_paniclog },
    { "antivirus", "Integrated AV + AI security status", "antivirus status|ai|suggest", 0, cmd_antivirus },
    { "threatlog", "Show recent threat detections", "threatlog", 0, cmd_threatlog },
    { "processinfo", "Show process monitor status", "processinfo", 0, cmd_processinfo },
    { "ps", "List live kernel tasks", "ps", 0, cmd_ps },
    { "kill", "Terminate a kernel task", "kill <pid>", 0, cmd_kill },
    { "exec", "Execute a built-in command in the current shell", "exec <command>", 0, cmd_exec },
    { "spawn", "Create a task and load an ELF32 user program", "spawn <file.elf>", 0, cmd_spawn },
    { "web", "Launch AxiomWeb Sovereign Browser", "web [url|about:portal]", 0, cmd_axiom_web },
    { "browser", "Launch default web browser (AxiomWeb)", "browser [url]", 0, cmd_axiom_web },
    { "axiom-web", "Launch AxiomWeb Sovereign Browser", "axiom-web [url]", 0, cmd_axiom_web },
    { "usertest", "Run the built-in user-mode lifecycle test", "usertest", 0, cmd_usertest },
    { "gfxdemo", "Run the user-space framebuffer desktop preview", "gfxdemo", 0, cmd_gfxdemo },
    { "gui", "Run the user-space LastGUI desktop preview", "gui", 0, cmd_gui },
    { "grep", "Filter pipe input by substring", "grep <text>", 0, cmd_grep },
    { "procctl", "Process control core (signals/wait/parent)", "procctl signal <task> <term|kill|usr1|usr2>|procctl wait <task>|procctl parent <task> <parent>|procctl show <task>", 0, cmd_procctl },
    { "module", "Manage modules/plugins", "module list|install <name>|uninstall <name>|load <name>|unload <name>|markgood <name>|rollback <name>", 0, cmd_module },
    { "lastmap", "Advanced network scanner", "lastmap --adv [flags] <target_ip>", "lastmap", cmd_lastmap },
    { "lastscan", "Simple network scan", "lastscan <target_ip>", "lastmap", cmd_lastscan },
    { "lastshark", "Packet analyzer (Wireshark-like)", "lastshark start|stop|stats|show [n]|filter all|tcp|udp|icmp|clear", "lastshark", cmd_lastshark },
    { "ping", "Send ICMP echo requests", "ping <host|ip> [-c count]", 0, cmd_ping },
    { "arp", "Show ARP neighbor table", "arp -a", 0, cmd_arp },
    { "curl", "Fetch an HTTP URL over TCP", "curl [http://]<host>[:port]/<path>", 0, cmd_curl },
    { "wget", "Download an HTTP resource into the persistent VFS", "wget [http://]<host>[:port]/<path> [-c] [-O file]", 0, cmd_wget },
    { "dig", "Resolve A and AAAA records (cached)", "dig <host>", 0, cmd_dig },
    { "nslookup", "Resolve a DNS name", "nslookup <host>", 0, cmd_dig },
    { "whois", "Query a WHOIS server over TCP", "whois <domain|ip>", 0, cmd_whois },
    { "traceroute", "Trace IPv4 hops using ICMP TTL expiry", "traceroute <host|ip> [max-hops]", 0, cmd_traceroute },
    { "netconsole", "Network management console (Cisco-style CLI)", "netconsole", 0, cmd_netconsole },
    { "dns-server", "Local DNS resolver on UDP:53", "dns-server start|stop|status", 0, cmd_dns_server },
    { "dhcp-server", "DHCP server with IP pool & lease management", "dhcp-server configure <pool_start> <pool_end> <mask> <gateway> [dns] [lease_ticks]|start|stop|status", 0, cmd_dhcp_server },
    { "nat", "Network Address Translation (NAT / port forwarding)", "nat [status|show]|forward on|off|dnat <tcp|udp> <ext_port> <int_ip> [int_port]|snat <net> <mask>|del <idx>|clear", 0, cmd_nat },
    { "tlsserver", "Run the BearSSL TLS test server", "tlsserver start <port>|poll|stop|status", 0, cmd_tlsserver },
    { "httpd", "HTTP/HTTPS static server and reverse proxy", "httpd start <port> [root]|start-tls <port> [root]|stop|status|proxy <prefix> <ip> <port>|noproxy", 0, cmd_httpd },
    { "firewall", "Manage firewall rules", "firewall allow <port>|deny <port>|list", "firewall", cmd_firewall },
    { "wim", "List files (friendly alias)", "wim", 0, cmd_wim },
    { "open", "Change directory (friendly alias)", "open <path>", 0, cmd_open },
    { "create", "Create file quickly", "create <name>", 0, cmd_create },
    { "vfsctl", "Kernel VFS permissions and journal", "vfsctl stats|flush|cred <uid> <gid>|chmod <path> <mode>|chown <path> <uid> <gid>|lock <path> <0|1>|access <path> <r|w|x|rw|rx|wx|rwx>|journal [n]", 0, cmd_vfsctl },
    { "df", "Show VFS capacity and usage", "df", 0, cmd_df },
    { "fsck", "Check and repair VFS metadata", "fsck [repair]", 0, cmd_fsck },
    { "fstest", "Run persistent filesystem tests", "fstest large", 0, cmd_fstest },
    { "edit", "Open text editor", "edit <name>", 0, cmd_edit },
    { "nano", "Nano-like persistent text editor", "nano <file>", 0, cmd_nano },
    { "write", "Alias of edit (write mode)", "write <name>", 0, cmd_write },
    { "show", "Show file content", "show <name>", 0, cmd_show },
    { "delete", "Delete file or folder", "delete <name>", 0, cmd_delete },
    { "rename", "Rename file or folder", "rename <old> <new>", 0, cmd_rename },
    { "touch", "Create empty file", "touch <name>", 0, cmd_touch },
    { "mkdir", "Create folder", "mkdir <name>", 0, cmd_mkdir },
    { "rmdir", "Remove empty folder", "rmdir <name>", 0, cmd_rmdir },
    { "resmon", "Show CPU/mem/net/io snapshot", "resmon", 0, cmd_resmon },
    { "rescheck", "Run lightweight resource stress check", "rescheck [loops] [bytes]", 0, cmd_rescheck },
    { "mode", "Set power mode", "mode performance|balanced|low-power|auto", 0, cmd_mode },
    { "sched", "Set scheduler policy", "sched status|sched task <id>|sched prio <task> <0..39>|sched rt <task> <on|off>|sched class <task> <cpu|io|net>|sched quantum <1|2|3>", 0, cmd_sched },
    { "schedlab", "Scheduler lab diagnostics and stress", "schedlab status|validate|reset|run <mode 0..3> [rounds]", 0, cmd_schedlab },
    { "iolab", "I/O+Network lab diagnostics and stress", "iolab status|validate|reset|run <mode 0..2> [rounds] [payload]", 0, cmd_iolab },
    { "seclab", "Security response lab diagnostics and stress", "seclab status|validate|reset|run <mode 0..2> [rounds]", 0, cmd_seclab },
    { "compatlab", "Compatibility lab diagnostics and stress", "compatlab status|validate|reset|run <mode 0..2> [rounds]", 0, cmd_compatlab },
    { "driverlab", "Driver/HAL lab diagnostics and stress", "driverlab status|validate|reset|run <mode 0..2> [rounds]", 0, cmd_driverlab },
    { "fusionlab", "Cross-layer fusion lab analytics", "fusionlab status|validate|reset|run [rounds]", 0, cmd_fusionlab },
    { "ipc", "Kernel message queue IPC", "ipc send <dst_task> <text>|ipc recv [task]|ipc stats|ipc reset", 0, cmd_ipc },
    { "profile", "Set adaptive resource profile", "profile desktop-light|desktop-full|server-light|server-full|auto", 0, cmd_profile },
    { "sandbox", "Set sandbox module quota", "sandbox quota <module> <memkb> <cpu%>|sandbox show <module>", 0, cmd_sandbox },
    { "sdc", "Smart Defense Core control", "sdc status|mode desktop|mode server", 0, cmd_sdc },
    { "kshield", "Kernel Shield X hardening control", "kshield status|mode balanced|mode hardened|lockdown <0..3>", 0, cmd_kshield },
    { "secmem", "Memory execution hardening (W^X/NX)", "secmem status|wx on|wx off", 0, cmd_secmem },
    { "hal", "Hardware abstraction and driver intelligence", "hal status|refresh|optimize|mode auto|mode desktop|mode server|ghost on|ghost off|render auto|render quality|render performance|driver update <device>|advisor <module>", "hal", cmd_hal },
    { "ultimate", "Stage 9 Ultimate engine control", "ultimate status|optimize|mode auto|mode desktop|mode server|mode vps|auth <user> <key>|tweak <key> <value>|rollback", "ultimate", cmd_ultimate },
    { "kcore", "Ultimate kernel orchestrator", "kcore status|optimize|suggest|preload desktop|preload server|preload vps|mode auto|mode desktop|mode server|mode vps|auth <user> <token>|tweak <key> <value>|rollback", 0, cmd_kcore },
    { "pcoe", "Predictive Core Optimization Engine 2.0", "pcoe status|suggest|mode auto|mode desktop|mode server|mode vps|preload desktop|preload server|preload vps|auth <user> <token>|tweak <key> <value>|rollback", "pcoe2", cmd_pcoe },
    { "compat", "UACE/DPK compatibility engine", "compat status|bind <pid> <lastos|windows|linux>|profile <pid>|pal <personality>|call <pid> <nr> [a0 a1 a2]|crash <pid> <reason>|sync <pid> [quick|deep]|autosync [budget]|prepare <pid> <pe32|pe64|elf32|elf64>|probe <pe32|pe64|elf32|elf64>", "compat", cmd_compat },
    { "linux", "Linux binary compatibility and sandboxing", "linux run <path>|test|info", "compat", cmd_linux },
    { "sys", "System dashboard actions", "sys refresh|optimize", 0, cmd_sys_widget },
    { "proc", "Process manager actions", "proc kill <pid>|proc monitor <pid>", 0, cmd_proc_widget },
    { "net", "Network monitor actions", "net status|net connect [ip mask gw]|net disconnect|net throttle <app>|net optimize", 0, cmd_net_widget },
    { "netstack", "Kernel network stack control", "netstack status|arp [n]|route [n]|route add <net> <mask> <gw> <metric>|sock open <tcp|udp> <lport> <rip> <rport>|sock send <id> [data]|sock recv <id>|sock close <id>|sock list", 0, cmd_netstack },
    { "audio", "AC'97 audio output", "audio status|beep <hz> <ms>|test", 0, cmd_audio },
    { "dns", "Configure DNS servers and cache", "dns show|set <slot 0..2> <ipv4>|reset|cache clear", 0, cmd_dns },
    { "vpn", "WireGuard VPN tunnel (in-kernel Noise_IK)", "vpn show|configure <endpoint-ip> <port> <private-key-hex> <peer-key-hex> <network> <mask>|start|stop|send <text>|clear", 0, cmd_vpn },
    { "av", "Antivirus widget actions", "av scan <path>|av quarantine <pid>", 0, cmd_av_widget },
    { "fw", "Firewall widget actions", "fw status|fw block <ip>|fw allow <app>", "firewall", cmd_fw_widget },
    { "logs", "Logs and alerts actions", "logs view <type>|logs export", 0, cmd_logs_widget },
    { "storage", "Storage widget actions", "storage analyze|storage optimize", 0, cmd_storage_widget },
    { "vps", "VPS instance actions", "vps list|vps start <id>|vps stop <id>", 0, cmd_vps_widget },
    { "predict", "Predictive analytics actions", "predict cpu|predict ram|predict network", 0, cmd_predict_widget },
    { "theme", "Theme control", "theme set <dark|light|gradient|custom>", 0, cmd_theme_widget },
    { "personalize", "Persistent personalization settings", "personalize status|theme <dark|light|gradient|custom>|wallpaper <0..4>|animations <on|off>|contrast <on|off>", 0, cmd_personalize },
    { "render", "Render control", "render optimize", "hal", cmd_render_widget },
    { "plugin", "Plugin manager actions", "plugin install <name>|plugin remove <name>|plugin update <name>", 0, cmd_plugin_widget },
    { "linuxmap", "Linux-to-LastOS compatibility helper", "linuxmap [show]", 0, cmd_linuxmap },
    { "trace", "Kernel trace ring buffer", "trace status|show [n]", 0, cmd_trace },
    { "drivers", "Driver bus status", "drivers list|stats|recover <name>|flags <name> <sandbox0|1> <critical0|1>", 0, cmd_drivers },
    { "disk", "Disk layer status", "disk status|list|select <id>|checksum <lba> <count>|detectos|read <lba>|write <lba> <text>", 0, cmd_disk },
    { "usb", "USB OHCI host-controller status", "usb status|scan", 0, cmd_usb },
    { "fb", "Framebuffer mode and resolution", "fb status|mode <width> <height>", 0, cmd_fb },
    { "layers", "Kernel/Driver/App layering status", "layers status", 0, cmd_layers },
    { "start", "Run a one-shot system action", "start install", 0, cmd_start },
    { "install", "LastOS installer engine", "install status|detect|quick|advanced|precheck|defaults|verify|testnet|rollback|wizard start <quick|advanced>|wizard next|wizard status|wizard commit|wizard cancel|set <lang|theme|mode|fs|part|guard|firewall|autoupdate> <value>", 0, cmd_install },
    { "recovery", "Recovery tools for filesystem and boot", "recovery status|fsck|repair-boot|rollback", 0, cmd_recovery },
    { "kinit", "Kernel modular init status", "kinit status|list [n]", 0, cmd_kinit },
    { "levels", "Project maturity levels and gates", "levels status|show [1..7]|target <1..7>", 0, cmd_levels },
    { "cgroup", "Manage resource groups", "cgroup list|create <name>|limit <id> <cpu> <memkb> <io>|bind <task> <id>", 0, cmd_cgroup },
    { "ns", "Manage namespaces", "ns list|create|bind <task> <nsid>", 0, cmd_ns },
    { "sysfilter", "Manage per-task syscall allowlists", "sysfilter status <task>|reset <task>|allow <task> <nr>|deny <task> <nr>", 0, cmd_sysfilter },
    { "mount", "Mount a Linux-compatible filesystem", "mount <path> <minix|ext2|ext4|vfat|ntfs> <dev> <start_lba>", 0, cmd_mount },
    { "umount", "Unmount a filesystem", "umount <path>", 0, cmd_umount },
    { "fstype", "Format a disk partition with a Linux-compatible filesystem", "fstype <minix|ext2|ext4|vfat> <start_lba> <total_lba>", 0, cmd_fstype },
    { "mkfs", "Format (alias for fstype)", "mkfs <minix|ext2|ext4|vfat> <start_lba> <total_lba>", 0, cmd_fstype },
    { "airmesh", "AirMesh Layer-2 Mesh Protocol (experimental)", "airmesh status|beacon|send <target_node> <msg>", 0, cmd_airmesh },
    { "neural", "Neural Predictive Prefetch AI Engine", "neural status|train", 0, cmd_neural },
    { "ghost", "HyperGhost Zero-Watt State & Memory Compression", "ghost status|freeze <task>|thaw <task>", 0, cmd_ghost }
};

#define CMD_COUNT ((u32)(sizeof(cmds) / sizeof(cmds[0])))
#define FW_RULE_MAX 32
#define CMD_HISTORY_MAX 32
#define CMD_HISTORY_LEN 128
#define VFS_NODE_MAX 100
#define VFS_NAME_MAX 24
#define VFS_TEXT_MAX 256

static void* last_alloc = 0;
static u8 spawn_image[VFS_FILE_LIMIT];
static fw_rule_t fw_rules[FW_RULE_MAX];
static u32 g_user_httpd_pid;
static u32 g_last_spawn_pid;
static char cmd_history[CMD_HISTORY_MAX][CMD_HISTORY_LEN];
static u32 cmd_history_head = 0;
static u32 cmd_history_count = 0;
static u8 cmd_history_loading = 0u;
static char cmd_env_names[24][24];
static char cmd_env_values[24][96];
static u32 cmd_env_count = 0u;

static char kvfs_cwd[96] = "/";
static const char* command_pipe_input = 0;
static u32 command_pipe_input_length = 0u;
static u32 command_composition_depth = 0u;

const char* commands_cwd(void) {
    return kvfs_cwd;
}

static void kvfs_normalize_path(const char* input, char* out, u32 out_sz) {
    char parts[16][VFS_MAX_NAME];
    u32 part_count = 0u, pos = 0u;
    char token[VFS_MAX_NAME];
    const char* p = input;
    if (!out || out_sz == 0u) return;
    while (p && *p) {
        while (*p == '/') p++;
        pos = 0u;
        while (*p && *p != '/') {
            if (pos + 1u < sizeof(token)) token[pos++] = *p;
            p++;
        }
        token[pos] = '\0';
        if (!token[0] || str_cmp(token, ".") == 0) continue;
        if (str_cmp(token, "..") == 0) {
            if (part_count) part_count--;
            continue;
        }
        if (part_count < 16u) {
            str_ncpy(parts[part_count], token, VFS_MAX_NAME - 1u);
            parts[part_count][VFS_MAX_NAME - 1u] = '\0';
            part_count++;
        }
    }
    str_ncpy(out, "/", out_sz);
    for (u32 i = 0u; i < part_count; i++) {
        if (str_cmp(out, "/") != 0) safe_append(out, out_sz, "/");
        safe_append(out, out_sz, parts[i]);
    }
}

static void kvfs_make_path(const char* raw, char* out, u32 out_sz) {
    const char* name = skip_space(raw);
    char combined[128];
    if (!out || out_sz == 0u) return;
    if (!name || !name[0]) {
        str_ncpy(out, kvfs_cwd, out_sz);
        out[out_sz - 1u] = '\0';
        return;
    }
    if (name[0] == '/') {
        kvfs_normalize_path(name, out, out_sz);
        return;
    }
    str_ncpy(combined, kvfs_cwd, sizeof(combined));
    combined[sizeof(combined) - 1u] = '\0';
    if (str_cmp(combined, "/") != 0) {
        safe_append(combined, sizeof(combined), "/");
    }
    safe_append(combined, sizeof(combined), name);
    kvfs_normalize_path(combined, out, out_sz);
}

static void kvfs_join_name(const char* name, char* out, u32 out_sz) {
    kvfs_make_path(name, out, out_sz);
}

typedef struct {
    u8 used;
    char name[24];
    u8 allow;
} fw_app_rule_t;

static fw_app_rule_t fw_app_rules[16];
static u8 vps_active[32];

static int env_name_valid(const char* name) {
    if (!name || !name[0]) return 0;
    for (u32 i = 0u; name[i]; i++) {
        if (!((name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= 'a' && name[i] <= 'z') ||
              (name[i] >= '0' && name[i] <= '9' && i > 0u) || name[i] == '_')) return 0;
    }
    return 1;
}

static int env_find(const char* name) {
    for (u32 i = 0u; i < cmd_env_count; i++) {
        if (str_cmp(cmd_env_names[i], name) == 0) return (int)i;
    }
    return -1;
}

static int env_set_value(const char* name, const char* value) {
    int idx;
    if (!env_name_valid(name) || !value) return -1;
    idx = env_find(name);
    if (idx < 0) {
        if (cmd_env_count >= 24u) return -1;
        idx = (int)cmd_env_count++;
        str_ncpy(cmd_env_names[idx], name, sizeof(cmd_env_names[idx]) - 1u);
        cmd_env_names[idx][sizeof(cmd_env_names[idx]) - 1u] = '\0';
    }
    str_ncpy(cmd_env_values[idx], value, sizeof(cmd_env_values[idx]) - 1u);
    cmd_env_values[idx][sizeof(cmd_env_values[idx]) - 1u] = '\0';
    return 0;
}

static void env_remove_value(const char* name) {
    int idx = env_find(name);
    if (idx < 0) return;
    for (u32 i = (u32)idx; i + 1u < cmd_env_count; i++) {
        str_cpy(cmd_env_names[i], cmd_env_names[i + 1u]);
        str_cpy(cmd_env_values[i], cmd_env_values[i + 1u]);
    }
    if (cmd_env_count) cmd_env_count--;
}

void commands_env_init(void) {
    cmd_env_count = 0u;
    (void)env_set_value("PATH", "/bin:/usr/bin:/usr/local/bin");
    (void)env_set_value("SHELL", "/bin/axiom");
    (void)env_set_value("TERM", "axiom-vga");
}

int commands_env_set(const char* name, const char* value) {
    return env_set_value(name, value);
}

int commands_env_expand(const char* input, char* output, u32 output_size) {
    u32 at = 0u;
    if (!input || !output || output_size == 0u) return -1;
    for (u32 i = 0u; input[i] && at + 1u < output_size; i++) {
        if (input[i] != '$') { output[at++] = input[i]; continue; }
        char name[24];
        u32 n = 0u;
        u32 j = i + 1u;
        if (input[j] == '$') { output[at++] = '$'; i = j; continue; }
        while (input[j] && ((input[j] >= 'A' && input[j] <= 'Z') || (input[j] >= 'a' && input[j] <= 'z') || (input[j] >= '0' && input[j] <= '9') || input[j] == '_') && n + 1u < sizeof(name)) name[n++] = input[j++];
        if (!n) { output[at++] = '$'; continue; }
        name[n] = '\0';
        int idx = env_find(name);
        if (idx >= 0) {
            for (u32 k = 0u; cmd_env_values[idx][k] && at + 1u < output_size; k++) output[at++] = cmd_env_values[idx][k];
        }
        i = j - 1u;
    }
    output[at] = '\0';
    return 0;
}

static void history_push(const char* line) {
    if (!line || !line[0]) {
        return;
    }
    str_ncpy(cmd_history[cmd_history_head], line, CMD_HISTORY_LEN);
    cmd_history_head = (cmd_history_head + 1u) % CMD_HISTORY_MAX;
    if (cmd_history_count < CMD_HISTORY_MAX) {
        cmd_history_count++;
    }
    if (!cmd_history_loading) {
        char record[CMD_HISTORY_LEN + 2u];
        u32 n = str_len(line);
        if (n > CMD_HISTORY_LEN - 1u) n = CMD_HISTORY_LEN - 1u;
        str_ncpy(record, line, n + 1u);
        record[n++] = '\n';
        record[n] = '\0';
        (void)kvfs_append_file("/var/log/lastos.history", record, n);
        (void)kvfs_flush_disk();
    }
}

void commands_history_init(void) {
    char data[CMD_HISTORY_MAX * CMD_HISTORY_LEN];
    u32 used;
    u32 start = 0u;
    cmd_history_head = 0u;
    cmd_history_count = 0u;
    cmd_history_loading = 1u;
    used = (u32)kvfs_read_file("/var/log/lastos.history", data, sizeof(data) - 1u);
    if ((int)used <= 0) { cmd_history_loading = 0u; return; }
    data[used] = '\0';
    for (u32 i = 0u; i <= used; i++) {
        if (data[i] == '\n' || data[i] == '\0') {
            if (i > start) {
                data[i] = '\0';
                history_push(data + start);
            }
            start = i + 1u;
        }
    }
    cmd_history_loading = 0u;
}

static void history_print(void) {
    if (cmd_history_count == 0) {
        kputs("No command history.\n");
        return;
    }
    for (u32 i = 0; i < cmd_history_count; i++) {
        u32 idx = (cmd_history_head + CMD_HISTORY_MAX - cmd_history_count + i) % CMD_HISTORY_MAX;
        print_u32(i + 1u);
        kputs(". ");
        kputs(cmd_history[idx]);
        kputc('\n');
    }
}

static const char* skip_space(const char* s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static int starts_with(const char* a, const char* b) {
    u32 i = 0;
    while (b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static void print_u64(u64 value) {
    char buf[24];
    int i = 0;
    if (value == 0u) {
        kputc('0');
        return;
    }
    while (value > 0u && i < (int)sizeof(buf) - 1) {
        buf[i++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (i > 0) {
        kputc(buf[--i]);
    }
}

static void print_u32(u32 value) {
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

static int parse_u32(const char* s, u32* out) {
    u32 v = 0;
    int seen = 0;

    s = skip_space(s);
    while (*s >= '0' && *s <= '9') {
        seen = 1;
        v = (v * 10u) + (u32)(*s - '0');
        s++;
    }
    if (!seen) {
        return -1;
    }
    *out = v;
    return 0;
}

static const char* read_token(const char* s, char* out, u32 out_sz) {
    u32 i = 0;
    s = skip_space(s);
    while (*s && *s != ' ' && *s != '\t' && i + 1 < out_sz) {
        out[i++] = *s++;
    }
    out[i] = '\0';
    return skip_space(s);
}

static void safe_copy(char* out, u32 out_sz, const char* text) {
    if (!out || out_sz == 0) return;
    if (!text) {
        out[0] = '\0';
        return;
    }
    str_ncpy(out, text, out_sz);
}

static void safe_append(char* out, u32 out_sz, const char* text) {
    u32 n;
    u32 i = 0;
    if (!out || out_sz == 0 || !text) {
        return;
    }
    n = str_len(out);
    if (n >= out_sz - 1u) {
        return;
    }
    while (text[i] && (n + i + 1u) < out_sz) {
        out[n + i] = text[i];
        i++;
    }
    out[n + i] = '\0';
}

static int linux_translate(const char* line, char* out, u32 out_sz, char* info, u32 info_sz) {
    char c1[24], c2[24];
    const char* rest;
    const char* args;

    out[0] = '\0';
    info[0] = '\0';
    if (!line || !line[0]) return 0;

    rest = read_token(line, c1, sizeof(c1));
    args = skip_space(rest);
    if (!c1[0]) return 0;
    (void)read_token(args, c2, sizeof(c2));

    if (str_cmp(c1, "processes") == 0) { safe_copy(out, out_sz, "processinfo"); return 1; }
    if (str_cmp(c1, "monitor") == 0) { safe_copy(out, out_sz, "resmon"); return 1; }
    if (str_cmp(c1, "terminate") == 0) { safe_copy(out, out_sz, "proc kill "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "priority") == 0) { safe_copy(out, out_sz, "sched prio "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "reprioritize") == 0) { safe_copy(out, out_sz, "sched prio "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "netconfig") == 0 || str_cmp(c1, "netip") == 0 || str_cmp(c1, "netstat") == 0) { safe_copy(out, out_sz, "net status"); return 1; }
    if (str_cmp(c1, "trace") == 0 || str_cmp(c1, "connect") == 0 || str_cmp(c1, "send") == 0 || str_cmp(c1, "fetch") == 0) { safe_copy(info, info_sz, "command is not implemented in the current kernel"); return 2; }
    if (str_cmp(c1, "protect") == 0 || str_cmp(c1, "own") == 0 || str_cmp(c1, "power") == 0) { safe_copy(out, out_sz, "antivirus suggest"); return 1; }
    if (str_cmp(c1, "usage") == 0 || str_cmp(c1, "memory") == 0 || str_cmp(c1, "disks") == 0) { safe_copy(out, out_sz, "meminfo"); return 1; }
    if (str_cmp(c1, "me") == 0) { safe_copy(out, out_sz, "echo user:lastos"); return 1; }
    if (str_cmp(c1, "identity") == 0 || str_cmp(c1, "usergroups") == 0 || str_cmp(c1, "switch") == 0) { safe_copy(out, out_sz, "echo identity subsystem: single-user stage"); return 1; }
    if (str_cmp(c1, "adduser") == 0 || str_cmp(c1, "password") == 0 || str_cmp(c1, "deluser") == 0) { safe_copy(out, out_sz, "echo single-user mode: user management unavailable"); return 1; }
    if (str_cmp(c1, "archive") == 0 || str_cmp(c1, "compress") == 0 || str_cmp(c1, "extract") == 0) { safe_copy(info, info_sz, "archive/compression is not implemented in the current kernel"); return 2; }
    if (str_cmp(c1, "time") == 0 || str_cmp(c1, "calendar") == 0) { safe_copy(out, out_sz, "date"); return 1; }
    if (str_cmp(c1, "say") == 0) { safe_copy(out, out_sz, "echo "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "past") == 0) { safe_copy(out, out_sz, "past"); return 1; }
    if (str_cmp(c1, "shortcut") == 0) { safe_copy(out, out_sz, "help"); return 1; }
    if (str_cmp(c1, "reset") == 0) { safe_copy(out, out_sz, "clear"); return 1; }
    if (str_cmp(c1, "attach") == 0) { safe_copy(out, out_sz, "module list"); return 1; }
    if (str_cmp(c1, "detach") == 0) { safe_copy(out, out_sz, "module list"); return 1; }
    if (str_cmp(c1, "openfiles") == 0) { safe_copy(out, out_sz, "processinfo"); return 1; }
    if (str_cmp(c1, "traceprocess") == 0) { safe_copy(out, out_sz, "processinfo"); return 1; }
    if (str_cmp(c1, "watch") == 0) { safe_copy(out, out_sz, "resmon"); return 1; }
    if (str_cmp(c1, "search") == 0 || str_cmp(c1, "compare") == 0 || str_cmp(c1, "order") == 0 ||
        str_cmp(c1, "unique") == 0 || str_cmp(c1, "toplines") == 0 || str_cmp(c1, "bottomlines") == 0) {
        safe_copy(info, info_sz, "text-processing command is not implemented in the current kernel"); return 2;
    }

    if (str_cmp(c1, "pwd") == 0) { safe_copy(out, out_sz, "pwd"); return 1; }
    if (str_cmp(c1, "ls") == 0) { safe_copy(out, out_sz, "ls "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "cd") == 0) { safe_copy(out, out_sz, "open "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "touch") == 0) { safe_copy(out, out_sz, "touch "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "mkdir") == 0) { safe_copy(out, out_sz, "mkdir "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "rm") == 0) { safe_copy(out, out_sz, "delete "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "rmdir") == 0) { safe_copy(out, out_sz, "rmdir "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "mv") == 0) { safe_copy(out, out_sz, "rename "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "cp") == 0) { safe_copy(out, out_sz, "cp "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "cat") == 0) { safe_copy(out, out_sz, "show "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "grep") == 0) { safe_copy(out, out_sz, "grep "); safe_append(out, out_sz, args); return 1; }

    if (str_cmp(c1, "ps") == 0) { safe_copy(out, out_sz, "ps"); return 1; }
    if (str_cmp(c1, "top") == 0) { safe_copy(out, out_sz, "resmon"); return 1; }
    if (str_cmp(c1, "kill") == 0) { safe_copy(out, out_sz, "kill "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "pkill") == 0) { safe_copy(info, info_sz, "pkill by name is not implemented; use proc kill <pid>"); return 2; }
    if (str_cmp(c1, "nice") == 0) { safe_copy(out, out_sz, "priority "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "renice") == 0) { safe_copy(out, out_sz, "reprioritize "); safe_append(out, out_sz, args); return 1; }

    if (str_cmp(c1, "ifconfig") == 0) { safe_copy(out, out_sz, "ifconfig"); return 1; }
    if (str_cmp(c1, "nslookup") == 0) { safe_copy(out, out_sz, "dig "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "ip") == 0 && str_cmp(c2, "addr") == 0) { safe_copy(out, out_sz, "net status"); return 1; }
    if (str_cmp(c1, "ping") == 0) { safe_copy(out, out_sz, "ping "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "traceroute") == 0) { safe_copy(out, out_sz, "traceroute "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "ssh") == 0 || str_cmp(c1, "scp") == 0) {
        safe_copy(info, info_sz, "network command is not implemented in the current kernel"); return 2;
    }
    if (str_cmp(c1, "curl") == 0) { safe_copy(out, out_sz, "curl "); safe_append(out, out_sz, args); return 1; }

    if (str_cmp(c1, "chmod") == 0 || str_cmp(c1, "chown") == 0 || str_cmp(c1, "sudo") == 0) {
        safe_copy(info, info_sz, "permission escalation command is not implemented; use vfsctl"); return 2;
    }
    if (str_cmp(c1, "ufw") == 0 && str_cmp(c2, "status") == 0) { safe_copy(out, out_sz, "firewall list"); return 1; }
    if (str_cmp(c1, "ufw") == 0 && str_cmp(c2, "allow") == 0) { safe_copy(out, out_sz, "firewall allow "); safe_append(out, out_sz, skip_space(args + 5)); return 1; }
    if (str_cmp(c1, "ufw") == 0 && str_cmp(c2, "deny") == 0) { safe_copy(out, out_sz, "firewall deny "); safe_append(out, out_sz, skip_space(args + 4)); return 1; }

    if (str_cmp(c1, "uname") == 0) { safe_copy(out, out_sz, "sysinfo"); return 1; }
    if (str_cmp(c1, "du") == 0) { safe_copy(info, info_sz, "du is not implemented; use df"); return 2; }
    if (str_cmp(c1, "free") == 0) { safe_copy(out, out_sz, "meminfo"); return 1; }
    if (str_cmp(c1, "lscpu") == 0) { safe_copy(out, out_sz, "cpuinfo"); return 1; }
    if (str_cmp(c1, "lsblk") == 0 || str_cmp(c1, "dmesg") == 0) { safe_copy(info, info_sz, "command is not implemented in the current kernel"); return 2; }

    if (str_cmp(c1, "whoami") == 0) { safe_copy(out, out_sz, "whoami"); return 1; }
    if (str_cmp(c1, "id") == 0) { safe_copy(out, out_sz, "id"); return 1; }
    if (str_cmp(c1, "adduser") == 0) { safe_copy(out, out_sz, "echo single-user mode: adduser unavailable"); return 1; }
    if (str_cmp(c1, "passwd") == 0) { safe_copy(out, out_sz, "echo single-user mode: passwd unavailable"); return 1; }
    if (str_cmp(c1, "deluser") == 0) { safe_copy(out, out_sz, "echo single-user mode: deluser unavailable"); return 1; }
    if (str_cmp(c1, "groups") == 0) { safe_copy(out, out_sz, "groups"); return 1; }
    if (str_cmp(c1, "su") == 0) { safe_copy(out, out_sz, "echo already running as root"); return 1; }

    if (str_cmp(c1, "tar") == 0 || str_cmp(c1, "gzip") == 0 || str_cmp(c1, "unzip") == 0) { safe_copy(info, info_sz, "archive/compression command is not implemented in the current kernel"); return 2; }
    if (str_cmp(c1, "date") == 0) { safe_copy(out, out_sz, "date"); return 1; }
    if (str_cmp(c1, "cal") == 0) { safe_copy(info, info_sz, "calendar formatting is not implemented; use date"); return 2; }
    if (str_cmp(c1, "echo") == 0) { safe_copy(out, out_sz, "echo "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "man") == 0) { safe_copy(out, out_sz, "help "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "history") == 0) { safe_copy(out, out_sz, "past"); return 1; }
    if (str_cmp(c1, "alias") == 0) { safe_copy(out, out_sz, "help"); return 1; }
    if (str_cmp(c1, "clear") == 0) { safe_copy(out, out_sz, "clear"); return 1; }

    if (str_cmp(c1, "mount") == 0) { safe_copy(out, out_sz, "mount "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "umount") == 0) { safe_copy(out, out_sz, "umount "); safe_append(out, out_sz, args); return 1; }
    if (str_cmp(c1, "lsof") == 0) { safe_copy(out, out_sz, "processinfo"); return 1; }
    if (str_cmp(c1, "strace") == 0) { safe_copy(out, out_sz, "processinfo"); return 1; }
    if (str_cmp(c1, "watch") == 0) { safe_copy(out, out_sz, "resmon"); return 1; }
    if (str_cmp(c1, "find") == 0 || str_cmp(c1, "diff") == 0 || str_cmp(c1, "sort") == 0 ||
        str_cmp(c1, "uniq") == 0 || str_cmp(c1, "head") == 0 || str_cmp(c1, "tail") == 0) {
        safe_copy(info, info_sz, "text-processing command is not implemented in the current kernel"); return 2;
    }

    return 0;
}

static const cmd_def_t* find_command(const char* name) {
    for (u32 i = 0; i < CMD_COUNT; i++) {
        if (str_cmp(name, cmds[i].name) == 0) {
            return &cmds[i];
        }
    }
    return 0;
}

static int module_available(const char* module_name) {
    if (!module_name) {
        return 1;
    }
    module_info_t info;
    if (kmodule_get(module_name, &info) != 0) {
        return 0;
    }
    return info.loaded ? 1 : 0;
}

static int with_sandbox(const char* module_name, cmd_fn fn, const char* args) {
    if (!module_name) {
        fn(args);
        return 0;
    }

    int token = ksandbox_open(module_name, 0, 0);
    if (token < 0) {
        kputs("Sandbox unavailable for module.\n");
        return -1;
    }
    fn(args);
    ksandbox_close(token);
    return 0;
}

static int pager_more(u32* line_count) {
    u32 rows = kconsole_rows();
    u32 page_lines = rows > 3u ? rows - 3u : 20u;
    int key;
    if (!line_count || *line_count < page_lines) return 1;

    kputs("-- More -- (Space: next, Enter: line, q: quit)");
    key = kgetc();
    kputc('\n');
    if (key == 'q' || key == 'Q' || key == 27) return 0;
    if (key == '\n' || key == '\r') {
        *line_count = page_lines - 1u;
    } else {
        *line_count = 0u;
    }
    return 1;
}

void commands_list(void) {
    u32 line_count = 0u;
    for (u32 i = 0; i < CMD_COUNT; i++) {
        if (!module_available(cmds[i].module)) {
            continue;
        }
        kputs(cmds[i].name);
        kputs(" - ");
        kputs(cmds[i].desc);
        kputc('\n');
        line_count++;
        if (!pager_more(&line_count)) return;
    }
}

const char* commands_suggest(const char* input) {
    if (!input || !input[0]) {
        return 0;
    }

    const char* best = 0;
    u32 best_prefix = 0;

    for (u32 i = 0; i < CMD_COUNT; i++) {
        if (!module_available(cmds[i].module)) {
            continue;
        }
        const char* n = cmds[i].name;
        u32 prefix = 0;
        while (input[prefix] && n[prefix] && input[prefix] == n[prefix]) {
            prefix++;
        }
        if (prefix > best_prefix) {
            best_prefix = prefix;
            best = n;
        }
    }

    if (best_prefix == 0) {
        return 0;
    }
    return best;
}

static int dispatch(const char* line) {
    for (u32 i = 0; i < CMD_COUNT; i++) {
        u32 nlen = str_len(cmds[i].name);
        if (str_cmp(line, cmds[i].name) == 0) {
            if (!module_available(cmds[i].module)) {
                kputs("Module not loaded. Use: module load ");
                kputs(cmds[i].module);
                kputc('\n');
                return 1;
            }
            with_sandbox(cmds[i].module, cmds[i].fn, "");
            return 1;
        }
        if (str_ncmp(line, cmds[i].name, nlen) == 0 && line[nlen] == ' ') {
            if (!module_available(cmds[i].module)) {
                kputs("Module not loaded. Use: module load ");
                kputs(cmds[i].module);
                kputc('\n');
                return 1;
            }
            with_sandbox(cmds[i].module, cmds[i].fn, skip_space(line + nlen));
            return 1;
        }
    }
    return 0;
}

static int commands_execute_simple_status(const char* line) {
    char mapped[160];
    char info[96];
    int t = 0;

    if (!line || !line[0]) {
        return 1;
    }
    history_push(line);

    if (str_cmp(line, "who made me") == 0) {
        kputs("LastOS v0.1\nMade by Taha\n");
        return 1;
    }
    t = linux_translate(line, mapped, sizeof(mapped), info, sizeof(info));
    if (t == 2) {
        kputs(info);
        kputc('\n');
        return 1;
    }
    if (t == 1 && mapped[0]) {
        if (dispatch(mapped)) {
            return 1;
        }
        kputs("Mapped command is not available yet: ");
        kputs(mapped);
        kputc('\n');
        return 0;
    }

    if (dispatch(line)) {
        return 1;
    }

    kputs("Unknown command. Type 'list' to see available commands.\n");
    const char* s = commands_suggest(line);
    if (s) {
        const cmd_def_t* c = find_command(s);
        kputs("Did you mean: ");
        kputs(s);
        kputs("?\n");
        if (c) {
            kputs("Hint: ");
            kputs(c->usage);
            kputc('\n');
        }
    }
    return 0;
}

static void shell_trim_copy(const char* start, u32 length, char* out, u32 out_size) {
    u32 begin = 0u;
    u32 end = length;
    u32 n = 0u;
    if (!out || out_size == 0u) return;
    while (begin < length && (start[begin] == ' ' || start[begin] == '\t')) begin++;
    while (end > begin && (start[end - 1u] == ' ' || start[end - 1u] == '\t')) end--;
    while (begin < end && n + 1u < out_size) out[n++] = start[begin++];
    out[n] = '\0';
}

static int shell_find_operator(const char* line, char op, u32* out_pos) {
    u8 quoted = 0u;
    if (!line || !out_pos) return 0;
    for (u32 i = 0u; line[i]; i++) {
        if (line[i] == '"') quoted = (u8)!quoted;
        if (!quoted && line[i] == op) {
            *out_pos = i;
            return 1;
        }
    }
    return 0;
}

static int commands_execute_composed(const char* line) {
    u32 pipe_pos = 0u;
    u32 redirect_pos = 0u;
    char left[192];
    char right[192];
    char output[8192];
    char path[128];
    u32 output_len;
    int append = 0;

    if (shell_find_operator(line, '|', &pipe_pos)) {
        shell_trim_copy(line, pipe_pos, left, sizeof(left));
        shell_trim_copy(line + pipe_pos + 1u, str_len(line + pipe_pos + 1u), right, sizeof(right));
        if (!left[0] || !right[0]) {
            kputs("shell: pipe requires a command on both sides\n");
            return 1;
        }
        kconsole_capture_begin(output, sizeof(output));
        command_composition_depth++;
        (void)commands_execute_status(left);
        command_composition_depth--;
        output_len = kconsole_capture_end();
        command_pipe_input = output;
        command_pipe_input_length = output_len;
        command_composition_depth++;
        (void)commands_execute_status(right);
        command_composition_depth--;
        command_pipe_input = 0;
        command_pipe_input_length = 0u;
        return 1;
    }

    if (shell_find_operator(line, '>', &redirect_pos)) {
        if (line[redirect_pos + 1u] == '>') append = 1;
        shell_trim_copy(line, redirect_pos, left, sizeof(left));
        shell_trim_copy(line + redirect_pos + (append ? 2u : 1u),
                        str_len(line + redirect_pos + (append ? 2u : 1u)), right, sizeof(right));
        if (!left[0] || !right[0]) {
            kputs("shell: redirect requires a command and file\n");
            return 1;
        }
        kvfs_make_path(right, path, sizeof(path));
        {
            vfs_node_info_t redirect_info;
            if (kvfs_info(path, &redirect_info) != 0 && kvfs_create_file(path) != 0) {
                kputs("shell: cannot create redirect target\n");
                return 1;
            }
        }
        kconsole_capture_begin(output, sizeof(output));
        command_composition_depth++;
        (void)commands_execute_status(left);
        command_composition_depth--;
        output_len = kconsole_capture_end();
        if ((append ? kvfs_append_file(path, output, output_len) : kvfs_write_file(path, output, output_len)) != 0) {
            kputs("shell: redirect write failed\n");
        } else {
            kputs("redirected ");
            print_u32(output_len);
            kputs(" bytes to ");
            kputs(path);
            kputc('\n');
        }
        return 1;
    }
    return 0;
}

int commands_execute_status(const char* line) {
    if (command_composition_depth == 0u && commands_execute_composed(line)) return 1;
    return commands_execute_simple_status(line);
}

int commands_execute_capture(const char* line, char* output, u32 output_size) {
    int rc;
    u32 used;
    if (!line || !output || output_size < 2u) return -1;
    output[0] = '\0';
    kconsole_capture_begin(output, output_size - 1u);
    rc = commands_execute_status(line);
    used = kconsole_capture_end();
    if (used >= output_size) used = output_size - 1u;
    output[used] = '\0';
    return rc;
}

void commands_execute(const char* line) {
    (void)commands_execute_status(line);
}


#include "commands_system.inc"
#include "commands_files.inc"
#include "commands_runtime.inc"
#include "commands_engines.inc"
#include "commands_network.inc"
#include "commands_misc.inc"
