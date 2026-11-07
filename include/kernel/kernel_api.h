#ifndef KERNEL_API_H
#define KERNEL_API_H

#include "types.h"
#include "stddef.h"
#include "cpu_detect.h"
#include "metrics.h"
#include "threat_logger.h"
#include "module.h"
#include "cgroup.h"
#include "namespace.h"
#include "power.h"
#include "scheduler.h"
#include "sdc.h"
#include "ai_security.h"
#include "hal.h"
#include "ultimate.h"
#include "ultimate_kernel.h"
#include "pcoe2.h"
#include "watchdog.h"
#include "vfs.h"
#include "file.h"
#include "driver_bus.h"
#include "disk.h"
#include "layers.h"
#include "netstack.h"
#include "kshield.h"
#include "slab.h"
#include "ipc.h"
#include "proc_core.h"
#include "memlab.h"
#include "schedlab.h"
#include "iolab.h"
#include "seclab.h"
#include "driverlab.h"
#include "fusionlab.h"
#include "kinit.h"
#include "mouse.h"
#include "levels.h"
#include "vm.h"
#include "hw_topology.h"
#include "smp.h"
#include "memory_runtime.h"
#include "rtc.h"
#include "entropy.h"
#include "elf_loader.h"
#include "gfx.h"

typedef void (*irq_handler_t)(void* regs);

void* kalloc(size_t size);
void kfree(void* ptr);
int register_interrupt(int num, void (*handler)());
void schedule(void);
void panic(const char* message);
u32 ksyscall(u32 nr, u64 a0, u64 a1, u64 a2, u64 a3);
int kcopy_from_user(void* dst, u64 user_src, u32 len);
int kcopy_to_user(u64 user_dst, const void* src, u32 len);
u8 kpaging_wx_enforced(void);
u8 kpaging_nx_capable(void);
u32 kpaging_wx_rejects(void);
void kpaging_set_wx(u8 enabled);

void kputc(char c);
void kputs(const char* s);
void kconsole_capture_begin(char* buffer, u32 capacity);
u32 kconsole_capture_end(void);
void kclear(void);
void ksetcolor(u8 color);
u8 kgetcolor(void);
void kresetcolor(void);
u32 kconsole_cols(void);
u32 kconsole_rows(void);
int kgetc(void);
int ktrygetc(void);
u32 kticks(void);
u32 ktimer_frequency(void);
int krtc_get_time(rtc_time_t* out);
u32 krtc_days_since_epoch(const rtc_time_t* time);
u32 krtc_seconds_since_midnight(const rtc_time_t* time);
void kentropy_fill(u8* out, u32 length);
const cpu_info_t* kcpuinfo(void);
void ksysinfo(kernel_metrics_t* out);
void kpersist_save(const char* text);
void kpersist_load(char* out, u32 out_size);
u32 kboot_count(void);
int kpersist_auth_exists(void);
void kpersist_set_auth(const char* user, const char* pass);
int kpersist_get_auth(char* user, u32 user_size, char* pass, u32 pass_size);
int kpersist_auth_verify(const char* user, const char* pass);
void kpersist_set_hostname(const char* hostname);
int kpersist_get_hostname(char* out, u32 out_size);
void kpersist_set_ui_prefs(
    u8 theme, u8 bg_preset, u8 animations, u8 high_contrast,
    u8 prompt_color, u8 input_color, u8 ok_color, u8 err_color, u8 text_color
);
int kpersist_get_ui_prefs(
    u8* theme, u8* bg_preset, u8* animations, u8* high_contrast,
    u8* prompt_color, u8* input_color, u8* ok_color, u8* err_color, u8* text_color
);
void kclip_set(const char* text);
void kclip_get(char* out, u32 out_size);
u32 kthreat_count(void);
int kthreat_get(u32 reverse_index, threat_record_t* out);
u32 kprocess_count(void);
u32 kprocess_current(void);
void kai_security_status(ai_security_status_t* out);
void khal_status(hal_status_t* out);
void khal_set_mode(hal_mode_t mode);
void khal_set_ghost_ram(u8 enabled);
void khal_set_render_policy(hal_render_policy_t policy);
int khal_driver_advice(const char* module_name, hal_driver_advice_t* out);
void kultimate_status(ultimate_status_t* out);
void kultimate_set_mode(ultimate_mode_t mode);
int kultimate_auth(const char* user, const char* key);
int kultimate_tweak(const char* key, u32 value);
void kultimate_rollback(void);
void kkernel_status(ukernel_status_t* out);
void kkernel_set_mode(ukernel_mode_t mode);
int kkernel_auth(const char* user, const char* token);
int kkernel_override(const char* key, u32 value);
void kkernel_rollback(void);
void kkernel_preload(ukernel_mode_t mode);
void kpcoe2_status(pcoe2_status_t* out);
void kpcoe2_set_mode(pcoe_mode_t mode);
int kpcoe2_auth(const char* user, const char* token);
int kpcoe2_override(const char* key, u32 value);
void kpcoe2_rollback(void);
void kpcoe2_preload(pcoe_mode_t mode);
int kmodule_load(const char* name);
int kmodule_unload(const char* name);
int kmodule_install(const char* name);
int kmodule_uninstall(const char* name);
int kmodule_mark_good(const char* name);
int kmodule_rollback(const char* name);
int kmodule_get(const char* name, module_info_t* out);
u32 kmodule_count(void);
int kmodule_get_by_index(u32 idx, module_info_t* out);
u32 khook_count(void);
u32 kselfheal_restarts(void);
int ksandbox_open(const char* module_name, u32 mem_limit_bytes, u32 tick_budget);
void ksandbox_close(int token);
int kcgroup_create(const char* name);
int kcgroup_set_limits(u32 id, u32 cpu_percent, u32 mem_kb, u32 io_ops);
int kcgroup_bind_task(u32 task_id, u32 cgroup_id);
u32 kcgroup_count(void);
int kcgroup_get_by_index(u32 idx, cgroup_info_t* out);
int knamespace_create(void);
int knamespace_bind_task(u32 task_id, u32 ns_id);
u32 knamespace_count(void);
int knamespace_get_by_index(u32 idx, namespace_info_t* out);
void kset_power_mode(power_mode_t mode);
power_mode_t kget_power_mode(void);
void kshutdown(void);
void kreboot(void);
int  kacpi_available(void);
int ksched_set_priority(u32 task_id, u8 prio);
int ksched_set_realtime(u32 task_id, int enabled);
int ksched_set_class(u32 task_id, sched_class_t task_class);
sched_class_t ksched_get_class(u32 task_id);
int ksched_kill(u32 task_id);
void ksched_set_quantum(u32 level);
u32 ksched_get_quantum_level(void);
u32 ksched_get_quantum_ticks(void);
void ksched_telemetry(scheduler_telemetry_t* out);
int ksched_task_telemetry(u32 task_id, scheduler_task_telemetry_t* out);
void ksched_policy_stats(scheduler_policy_stats_t* out);
int ksandbox_set_quota(const char* module_name, u32 mem_limit_kb, u32 cpu_limit_percent);
int ksandbox_get_quota(const char* module_name, u32* mem_limit_kb, u32* cpu_limit_percent);
void ksdc_status(sdc_status_t* out);
void ksdc_mode(sdc_mode_t mode);
sdc_mode_t ksdc_get_mode(void);
int ksdc_register_hook(const char* module_name, hook_fn_t fn);
void kwatchdog_status(watchdog_status_t* out);
void kwatchdog_policy(watchdog_policy_t* out);
void kwatchdog_set_policy(const watchdog_policy_t* policy);
u32 kvfs_list(const char* path, vfs_node_info_t* out, u32 max_out);
int kvfs_info(const char* path, vfs_node_info_t* out);
int kvfs_mkdir(const char* path);
int kvfs_create_file(const char* path);
int kvfs_delete(const char* path);
int kvfs_rename(const char* path, const char* new_name);
int kvfs_read_file(const char* path, char* out, u32 out_len);
int kvfs_read_file_at(const char* path, u32 offset, char* out, u32 out_len);
int kvfs_write_file(const char* path, const char* data, u32 len);
int kvfs_append_file(const char* path, const char* data, u32 len);
void kvfs_begin_batch(void);
int kvfs_end_batch(void);
int kvfs_flush_disk(void);
int kvfs_rollback_disk(void);
int kvfs_reload_disk(void);
int kpdisk_test_torn_vfs_wal(void);
int kpdisk_wal_recover(void);
int kvfs_fsck(vfs_fsck_report_t* out, u8 repair);
int kvfs_usage(u32* total_bytes, u32* used_bytes, u32* free_bytes);
int kpersistent_disk_ready(void);
int kfile_open(const char* path, u32 flags, u32* out_fd);
int kfile_close(u32 fd);
int kfile_dup(u32 fd, u32* out_fd);
int kfile_read(u32 fd, void* out, u32 len);
int kfile_write(u32 fd, const void* data, u32 len);
int kfile_seek(u32 fd, i32 offset, u32 whence, u32* out_pos);
int kfile_stat(u32 fd, vfs_node_info_t* out);
int kfile_info(u32 fd, kfile_info_t* out);
int kfile_sync(u32 fd);
void kvfs_stats(vfs_stats_t* out);
void kvfs_set_credentials(u32 uid, u32 gid);
int kvfs_set_permissions(const char* path, u16 mode);
int kvfs_set_owner(const char* path, u32 uid, u32 gid);
int kvfs_set_lock(const char* path, u8 locked);
int kvfs_access(const char* path, u8 access_mask);
u32 kvfs_journal_count(void);
int kvfs_journal_get(u32 reverse_index, vfs_journal_entry_t* out);
u32 kdriver_count(void);
int kdriver_info(u32 idx, driver_info_t* out);
int kdriver_recover(const char* name);
int kdriver_set_flags(const char* name, u8 sandboxed, u8 critical);
void kdriver_bus_stats(driver_bus_stats_t* out);
int kdriver_read(const char* name, u8* out, u32 len);
int kdriver_write(const char* name, const u8* in, u32 len);
u32 kdisk_count(void);
int kdisk_info(u32 idx, disk_info_t* out);
int kdisk_select(u32 idx);
int kdisk_read_sector(u32 lba, u8* out512);
int kdisk_write_sector(u32 lba, const u8* in512);
u32 kdisk_checksum(u32 start_lba, u32 count);
int kdisk_detect_other_os(void);
int kdisk_partition_info(disk_partition_info_t* out);
u32 kdisk_partition_count(void);
int kdisk_partition_get(u32 index, disk_partition_info_t* out);
int kdisk_free_space(disk_free_space_t* out);
void kmouse_state(mouse_state_t* out);
void klayers_status(layer_stats_t* out);
void knetstack_status(netstack_status_t* out);
void knetstack_poll(u32 budget);
u32 knetstack_arp_count(void);
int knetstack_arp_get(u32 idx, net_arp_entry_t* out);
int knetstack_arp_resolve(u32 ip, u32 timeout_ticks);
int knetstack_prepare_peer(u32 remote_ip, u32 timeout_ticks);
int knetstack_ping(u32 remote_ip, u16 sequence, u32 timeout_ticks, u32* out_ticks, u8* out_ttl);
int knetstack_traceroute_probe(u32 remote_ip, u8 hop_limit, u16 sequence, u32 timeout_ticks,
                               u32* out_hop_ip, u32* out_ticks, u8* out_reached);
int knetstack_ipv6_local(u8 out_addr[16]);
int knetstack_ipv6_ping(const u8 remote[16], u16 sequence, u32 timeout_ticks, u32* out_ticks);
u32 knetstack_route_count(void);
int knetstack_route_get(u32 idx, net_route_entry_t* out);
int knetstack_route_add(u32 network, u32 mask, u32 gateway, u32 metric);
int knetstack_socket_open(u32 proto, u16 local_port, u32 remote_ip, u16 remote_port);
int knetstack_socket_open6(u32 proto, u16 local_port, const u8 remote_ip[16], u16 remote_port);
int knetstack_socket_listen(u16 local_port);
int knetstack_socket_accept(u32 listener_id, u32* out_client_id, u32* out_remote_ip, u16* out_remote_port);
int knetstack_socket_close(u32 sock_id);
int knetstack_socket_send(u32 sock_id, const u8* data, u16 len);
int knetstack_socket_recv(u32 sock_id, u8* out, u16 out_len, u32* out_src_ip, u16* out_src_port);
int knetstack_socket_recv6(u32 sock_id, u8* out, u16 out_len, u8 out_src_ipv6[16], u16* out_src_port);
u32 knetstack_socket_count(void);
int knetstack_socket_get(u32 idx, net_socket_info_t* out);
int knetstack_socket_state(u32 sock_id, u8* out_state);
int knetstack_socket_set_owner(u32 sock_id, u32 owner_task);
void knetstack_socket_release_task(u32 task_id);
void knetstack_set_local(u32 ip, u32 mask, u32 gateway);

void ksyscall_policy_reset(u32 task_id);
int ksyscall_policy_set(u32 task_id, u32 nr, u8 allowed);
int ksyscall_policy_allowed(u32 task_id, u32 nr);
u32 ksyscall_policy_denied_count(u32 task_id);
int kelf_load_user(const u8* image, u32 image_len, u32 task_id, elf_exec_info_t* out);
int kelf_clone_user(u32 parent_task, u32 child_task, elf_exec_info_t* out);
int kscheduler_create_user_task(const char* name, u32* out_task_id);
void kscheduler_request_user_switch(void);
void kkshield_status(kshield_status_t* out);
void kkshield_mode(kshield_mode_t mode);
void kkshield_lockdown(u8 level);
u32 kframe_largest_order(void);
u32 kframe_fragmentation(void);
u32 kslab_class_count(void);
int kslab_class_stats(u32 idx, slab_class_stats_t* out);
u32 kadv_alloc_failures(void);
int kipc_send(u32 src_task, u32 dst_task, const char* msg);
int kipc_recv(u32 dst_task, ipc_message_t* out);
void kipc_stats(ipc_stats_t* out);
void kipc_reset(void);
void kproc_register(u32 task_id, u32 parent_task);
int kproc_signal_send(u32 task_id, proc_signal_t sig);
int kproc_wait(u32 task_id, i32* out_exit_code);
int kproc_wait_any(u32 parent_task, u32* out_child_task, i32* out_exit_code);
int kproc_has_children(u32 parent_task);
void kproc_set_parent(u32 task_id, u32 parent_task);
u32 kproc_parent(u32 task_id);
int kproc_is_descendant(u32 task_id, u32 ancestor_task);
int kproc_status(u32 task_id, proc_status_t* out);
void kmemlab_status(memlab_snapshot_t* out);
void kmemlab_reset(void);
int kmemlab_validate(memlab_validation_t* out);
int kmemlab_run(u32 scenario, u32 rounds, u32 payload, memlab_run_result_t* out);
u32 kmemlab_hist_bins(void);
u32 kmemlab_hist_bin(u32 idx);
void kschedlab_status(schedlab_snapshot_t* out);
void kschedlab_reset(void);
int kschedlab_validate(schedlab_validation_t* out);
int kschedlab_run(u32 mode, u32 rounds, schedlab_run_result_t* out);
void kiolab_status(iolab_snapshot_t* out);
void kiolab_reset(void);
int kiolab_validate(iolab_validation_t* out);
int kiolab_run(u32 mode, u32 rounds, u32 payload, iolab_run_result_t* out);
void kseclab_status(seclab_snapshot_t* out);
void kseclab_reset(void);
int kseclab_validate(seclab_validation_t* out);
int kseclab_run(u32 mode, u32 rounds, seclab_run_result_t* out);
void kdriverlab_status(driverlab_snapshot_t* out);
void kdriverlab_reset(void);
int kdriverlab_validate(driverlab_validation_t* out);
int kdriverlab_run(u32 mode, u32 rounds, driverlab_run_result_t* out);
void kfusionlab_status(fusionlab_snapshot_t* out);
void kfusionlab_reset(void);
int kfusionlab_validate(fusionlab_validation_t* out);
int kfusionlab_run(u32 rounds, fusionlab_run_result_t* out);
void kkinit_status(kinit_status_t* out);
int kkinit_module_info(u32 idx, kinit_module_t* out, i32* out_rc);
void kvm_stats(vm_stats_t* out);
void kmemrt_status(memrt_snapshot_t* out);
int kmemrt_vault_store(const char* data, u32* out_handle);
int kmemrt_vault_load(u32 handle, char* out, u32 out_len);
int kmemrt_vault_zeroize(u32 handle);
int kmemrt_zero_copy_create(u32 kind, void* ptr, u32 len, u32* out_id);
void klevels_status(level_status_t* out);
int klevels_info(u8 level_id, level_info_t* out);
int klevels_target(u8 level_id);
void khw_topology_status(hw_topology_status_t* out);
u32 khw_core_temperature(u32 core);
u32 khw_core_numa_node(u32 core);
void ksmp_status(smp_status_t* out);

#include "dns.h"
#include "dns_server.h"
#include "dhcp_server.h"
int  kdns_resolve_ipv4(const char* name, u32* out_ip, u32 timeout_ticks);
int  kdns_server_start(void);
void kdns_server_stop(void);
int  kdns_server_active(void);
void kdns_server_status(dns_server_status_t* out);

int  kdhcp_server_configure(u32 pool_start, u32 pool_end, u32 subnet_mask, u32 gateway, u32 dns_server, u32 lease_ticks);
int  kdhcp_server_start(void);
void kdhcp_server_stop(void);
int  kdhcp_server_active(void);
int  kdhcp_server_lease_get(u32 index, dhcp_lease_t* out);
u32  kdhcp_server_lease_count(void);
void kdhcp_server_status(dhcp_server_status_t* out);

/* NAT / Port Forwarding */
#include "nat.h"
void knat_init(void);
int  knat_rule_add(u8 type, u8 proto, u16 external_port, u32 internal_ip, u16 internal_port, u32 internal_net, u32 internal_mask);
int  knat_rule_del(u32 index);
int  knat_rule_get(u32 index, nat_rule_t* out);
u32  knat_rule_count(void);
void knat_set_ip_forward(u8 on);
u8   knat_ip_forward(void);
u32  knat_conntrack_count(void);
int  knat_conntrack_get(u32 index, conntrack_entry_t* out);
void knat_status(nat_status_t* out);

#include "mount.h"
int kmount(const char* mount_point, const char* fstype, u32 dev_id, u32 start_lba);
int kumount(const char* mount_point);
int kformat(const char* fstype, u32 dev_id, u32 start_lba, u32 total_lba);
int kmount_list(mount_entry_t* out, u32 max_out);
u32 kmount_count(void);

#endif
