# ==============================================================================
# LastKernel — Pure Standalone 64-Bit Monolithic OS Kernel Makefile
# Architectures: x86_64 SMP
# ==============================================================================

CC      := gcc
AS      := nasm
LD      := ld
RUSTC   := rustc

CFLAGS  := -m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -ffreestanding -fno-pic -fstack-protector-strong \
           -mstack-protector-guard=global -nostdlib -nostdinc \
           -Iinclude -Wall -Wextra -O2

ASFLAGS := -f elf64
LDFLAGS := -T linker.ld -nostdlib

BUILD_DIR := build
RUST_LIB  := $(BUILD_DIR)/librust_kernel.a

# ------------------------------------------------------------------------------
# Kernel Objects
# ------------------------------------------------------------------------------
ASM_OBJS := \
	$(BUILD_DIR)/kernel_entry.o \
	$(BUILD_DIR)/gdt_flush.o \
	$(BUILD_DIR)/ap_trampoline.o \
	$(BUILD_DIR)/switch_to.o \
	$(BUILD_DIR)/interrupt_stubs.o

C_OBJS := \
	$(BUILD_DIR)/bootinfo.o \
	$(BUILD_DIR)/kernel.o \
	$(BUILD_DIR)/stack_protector.o \
	$(BUILD_DIR)/core.o \
	$(BUILD_DIR)/ultimate_kernel.o \
	$(BUILD_DIR)/kthread.o \
	$(BUILD_DIR)/gdt.o \
	$(BUILD_DIR)/idt.o \
	$(BUILD_DIR)/interrupts.o \
	$(BUILD_DIR)/cpu_detect.o \
	$(BUILD_DIR)/hw_topology.o \
	$(BUILD_DIR)/smp.o \
	$(BUILD_DIR)/paging.o \
	$(BUILD_DIR)/vm.o \
	$(BUILD_DIR)/frame_allocator.o \
	$(BUILD_DIR)/heap.o \
	$(BUILD_DIR)/memory_runtime.o \
	$(BUILD_DIR)/slab.o \
	$(BUILD_DIR)/page_cache.o \
	$(BUILD_DIR)/advanced_allocator.o \
	$(BUILD_DIR)/hyper_ghost.o \
	$(BUILD_DIR)/swap.o \
	$(BUILD_DIR)/watchdog.o \
	$(BUILD_DIR)/vma.o \
	$(BUILD_DIR)/oom.o \
	$(BUILD_DIR)/scheduler.o \
	$(BUILD_DIR)/quantum_scheduler.o \
	$(BUILD_DIR)/multicore_scheduler.o \
	$(BUILD_DIR)/sched_runtime.o \
	$(BUILD_DIR)/task.o \
	$(BUILD_DIR)/wait.o \
	$(BUILD_DIR)/syscall.o \
	$(BUILD_DIR)/apic.o \
	$(BUILD_DIR)/power.o \
	$(BUILD_DIR)/timer.o \
	$(BUILD_DIR)/hpet.o \
	$(BUILD_DIR)/rtc.o \
	$(BUILD_DIR)/pci.o \
	$(BUILD_DIR)/driver_bus.o \
	$(BUILD_DIR)/ahci.o \
	$(BUILD_DIR)/nvme.o \
	$(BUILD_DIR)/ide.o \
	$(BUILD_DIR)/virtio_blk.o \
	$(BUILD_DIR)/virtio_pci.o \
	$(BUILD_DIR)/virtio_ring.o \
	$(BUILD_DIR)/vfs.o \
	$(BUILD_DIR)/devfs.o \
	$(BUILD_DIR)/disk.o \
	$(BUILD_DIR)/mount.o \
	$(BUILD_DIR)/pipe.o \
	$(BUILD_DIR)/ext2.o \
	$(BUILD_DIR)/fat.o \
	$(BUILD_DIR)/ntfs.o \
	$(BUILD_DIR)/minix.o \
	$(BUILD_DIR)/persistent_disk.o \
	$(BUILD_DIR)/lastfs.o \
	$(BUILD_DIR)/file.o \
	$(BUILD_DIR)/e1000.o \
	$(BUILD_DIR)/rtl8169.o \
	$(BUILD_DIR)/rtl8139.o \
	$(BUILD_DIR)/pcnet.o \
	$(BUILD_DIR)/virtio_net.o \
	$(BUILD_DIR)/iwlwifi.o \
	$(BUILD_DIR)/bluetooth.o \
	$(BUILD_DIR)/netstack.o \
	$(BUILD_DIR)/nic.o \
	$(BUILD_DIR)/ip.o \
	$(BUILD_DIR)/ipv6.o \
	$(BUILD_DIR)/ipv6_transport.o \
	$(BUILD_DIR)/ethernet.o \
	$(BUILD_DIR)/tcp_raw.o \
	$(BUILD_DIR)/udp_raw.o \
	$(BUILD_DIR)/dns.o \
	$(BUILD_DIR)/dhcp.o \
	$(BUILD_DIR)/checksum.o \
	$(BUILD_DIR)/nat.o \
	$(BUILD_DIR)/dns_server.o \
	$(BUILD_DIR)/dhcp_server.o \
	$(BUILD_DIR)/airmesh.o \
	$(BUILD_DIR)/wireguard.o \
	$(BUILD_DIR)/httpd.o \
	$(BUILD_DIR)/xhci.o \
	$(BUILD_DIR)/usb_ehci.o \
	$(BUILD_DIR)/usb_ohci.o \
	$(BUILD_DIR)/usb_msc.o \
	$(BUILD_DIR)/keyboard.o \
	$(BUILD_DIR)/mouse.o \
	$(BUILD_DIR)/touchpad.o \
	$(BUILD_DIR)/printer.o \
	$(BUILD_DIR)/serial.o \
	$(BUILD_DIR)/screen.o \
	$(BUILD_DIR)/pty.o \
	$(BUILD_DIR)/hdaudio.o \
	$(BUILD_DIR)/ac97.o \
	$(BUILD_DIR)/pcspeaker.o \
	$(BUILD_DIR)/vbe.o \
	$(BUILD_DIR)/virtio_gpu.o \
	$(BUILD_DIR)/i915.o \
	$(BUILD_DIR)/gpu_drm.o \
	$(BUILD_DIR)/gpu_backend.o \
	$(BUILD_DIR)/display_mst.o \
	$(BUILD_DIR)/string.o \
	$(BUILD_DIR)/entropy.o \
	$(BUILD_DIR)/spinlock.o \
	$(BUILD_DIR)/rcu.o \
	$(BUILD_DIR)/security.o \
	$(BUILD_DIR)/memory_protect.o \
	$(BUILD_DIR)/task_protect.o \
	$(BUILD_DIR)/irq_sanitizer.o \
	$(BUILD_DIR)/audit.o \
	$(BUILD_DIR)/antivirus.o \
	$(BUILD_DIR)/ai_security.o \
	$(BUILD_DIR)/kshield.o \
	$(BUILD_DIR)/security_hook.o \
	$(BUILD_DIR)/self_heal.o \
	$(BUILD_DIR)/fde.o \
	$(BUILD_DIR)/sha256.o \
	$(BUILD_DIR)/tpm2.o \
	$(BUILD_DIR)/epoll.o \
	$(BUILD_DIR)/io_uring.o \
	$(BUILD_DIR)/io_scheduler.o \
	$(BUILD_DIR)/ebpf.o \
	$(BUILD_DIR)/kprobes.o \
	$(BUILD_DIR)/shm.o \
	$(BUILD_DIR)/ipc.o \
	$(BUILD_DIR)/cgroup.o \
	$(BUILD_DIR)/namespace.o \
	$(BUILD_DIR)/sandbox.o \
	$(BUILD_DIR)/resource_monitor.o \
	$(BUILD_DIR)/resource_engine.o \
	$(BUILD_DIR)/process_monitor.o \
	$(BUILD_DIR)/memory_monitor.o \
	$(BUILD_DIR)/io_guard.o \
	$(BUILD_DIR)/threat_logger.o \
	$(BUILD_DIR)/paniclog.o \
	$(BUILD_DIR)/signature_updater.o \
	$(BUILD_DIR)/kernel_monitor.o \
	$(BUILD_DIR)/lastmap.o \
	$(BUILD_DIR)/lastshark.o \
	$(BUILD_DIR)/metrics.o \
	$(BUILD_DIR)/sysinfo.o \
	$(BUILD_DIR)/persist.o \
	$(BUILD_DIR)/clipboard.o \
	$(BUILD_DIR)/secure_patch.o \
	$(BUILD_DIR)/ktrace.o \
	$(BUILD_DIR)/module.o \
	$(BUILD_DIR)/kinit.o \
	$(BUILD_DIR)/proc_core.o \
	$(BUILD_DIR)/pkg_manager.o \
	$(BUILD_DIR)/controller.o \
	$(BUILD_DIR)/banner.o \
	$(BUILD_DIR)/connect_scan.o \
	$(BUILD_DIR)/multicore_dispatch.o \
	$(BUILD_DIR)/rate_limiter.o \
	$(BUILD_DIR)/scan_table.o \
	$(BUILD_DIR)/syn_scan.o \
	$(BUILD_DIR)/timing_engine.o \
	$(BUILD_DIR)/udp_scan.o \
	$(BUILD_DIR)/plugin.o \
	$(BUILD_DIR)/memlab.o \
	$(BUILD_DIR)/schedlab.o \
	$(BUILD_DIR)/iolab.o \
	$(BUILD_DIR)/seclab.o \
	$(BUILD_DIR)/compatlab.o \
	$(BUILD_DIR)/driverlab.o \
	$(BUILD_DIR)/fusionlab.o \
	$(BUILD_DIR)/rust_bridge.o \
	$(BUILD_DIR)/layers.o \
	$(BUILD_DIR)/levels.o \
	$(BUILD_DIR)/pcoe2.o \
	$(BUILD_DIR)/pcoe2_core.o \
	$(BUILD_DIR)/pcoe2_matrix.o \
	$(BUILD_DIR)/compat.o \
	$(BUILD_DIR)/compat_engine.o \
	$(BUILD_DIR)/compat_astg.o \
	$(BUILD_DIR)/compat_bbf.o \
	$(BUILD_DIR)/compat_mmul.o \
	$(BUILD_DIR)/compat_sece.o \
	$(BUILD_DIR)/compat_dllsync.o \
	$(BUILD_DIR)/compat_bridge.o \
	$(BUILD_DIR)/linux_compat.o \
	$(BUILD_DIR)/linux_rootfs.o \
	$(BUILD_DIR)/wayland_bridge.o \
	$(BUILD_DIR)/hal.o \
	$(BUILD_DIR)/ultimate.o \
	$(BUILD_DIR)/universal_runtime.o \
	$(BUILD_DIR)/sdc.o \
	$(BUILD_DIR)/selftest.o \
	$(BUILD_DIR)/gfx.o \
	$(BUILD_DIR)/secure_io.o \
	$(BUILD_DIR)/userdb.o \
	$(BUILD_DIR)/elf_loader.o \
	$(BUILD_DIR)/tls_server.o \
	$(BUILD_DIR)/tls_identity.o \
	$(BUILD_DIR)/tls_bearssl.o \
	$(BUILD_DIR)/bearssl_entropy.o \
	$(BUILD_DIR)/shell.o \
	$(BUILD_DIR)/commands.o \
	$(BUILD_DIR)/shell_threatlog.o \
	$(BUILD_DIR)/shell_processinfo.o \
	$(BUILD_DIR)/shell_sysinfo_view.o

ALL_OBJS := $(ASM_OBJS) $(C_OBJS) $(RUST_LIB)

.PHONY: all iso clean qemu test

all: $(BUILD_DIR)/kernel.elf

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ------------------------------------------------------------------------------
# Assembly Rules
# ------------------------------------------------------------------------------
$(BUILD_DIR)/kernel_entry.o: arch/x86_64/boot/kernel_entry.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/gdt_flush.o: arch/x86_64/boot/gdt_flush.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/ap_trampoline.o: arch/x86_64/boot/ap_trampoline.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/switch_to.o: arch/x86_64/boot/switch_to.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/interrupt_stubs.o: arch/x86_64/cpu/interrupt_stubs.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

# ------------------------------------------------------------------------------
# Rust Kernel Lib
# ------------------------------------------------------------------------------
$(RUST_LIB): $(wildcard rust_kernel/src/*.rs) | $(BUILD_DIR)
	$(RUSTC) --crate-type staticlib --target x86_64-unknown-none \
		-C panic=abort -C opt-level=2 -C relocation-model=static \
		-o $@ rust_kernel/src/lib.rs

# ------------------------------------------------------------------------------
# C Source Rules
# ------------------------------------------------------------------------------
$(BUILD_DIR)/bootinfo.o: arch/x86_64/boot/bootinfo.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.o: kernel/kernel.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/stack_protector.o: kernel/stack_protector.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/core.o: kernel/core.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ultimate_kernel.o: kernel/ultimate_kernel.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kthread.o: kernel/sched/kthread.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt.o: arch/x86_64/cpu/gdt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: arch/x86_64/cpu/idt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupts.o: arch/x86_64/cpu/interrupts.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cpu_detect.o: arch/x86_64/cpu/cpu_detect.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hw_topology.o: arch/x86_64/cpu/hw_topology.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/smp.o: arch/x86_64/cpu/smp.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/paging.o: arch/x86_64/mmu/paging.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vm.o: mm/vm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/frame_allocator.o: mm/frame_allocator.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/heap.o: mm/heap.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/memory_runtime.o: mm/memory_runtime.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/slab.o: mm/slab.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/page_cache.o: mm/page_cache.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/advanced_allocator.o: mm/advanced_allocator.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hyper_ghost.o: mm/hyper_ghost.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/swap.o: mm/swap.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/watchdog.o: mm/watchdog.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vma.o: mm/vma.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/oom.o: mm/oom.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/scheduler.o: kernel/sched/scheduler.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/quantum_scheduler.o: kernel/sched/quantum_scheduler.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/multicore_scheduler.o: kernel/sched/multicore_scheduler.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sched_runtime.o: kernel/sched/sched_runtime.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/task.o: kernel/sched/task.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/wait.o: kernel/sched/wait.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syscall.o: kernel/syscall/syscall.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apic.o: kernel/power/apic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/power.o: kernel/power/power.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/timer.o: kernel/time/timer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hpet.o: kernel/time/hpet.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rtc.o: kernel/time/rtc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pci.o: drivers/bus/pci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/driver_bus.o: drivers/bus/driver_bus.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ahci.o: drivers/block/ahci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/nvme.o: drivers/block/nvme.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ide.o: drivers/block/ide.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/virtio_blk.o: drivers/block/virtio_blk.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/virtio_pci.o: drivers/block/virtio_pci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/virtio_ring.o: drivers/block/virtio_ring.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vfs.o: fs/vfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/devfs.o: fs/devfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/disk.o: fs/disk.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mount.o: fs/mount.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pipe.o: fs/pipe.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ext2.o: fs/ext2.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fat.o: fs/fat.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ntfs.o: fs/ntfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/minix.o: fs/minix.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/persistent_disk.o: fs/persistent_disk.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lastfs.o: fs/lastfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/file.o: fs/file.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/e1000.o: drivers/net/e1000.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rtl8169.o: drivers/net/rtl8169.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rtl8139.o: drivers/net/rtl8139.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pcnet.o: drivers/net/pcnet.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/virtio_net.o: drivers/net/virtio_net.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/iwlwifi.o: drivers/net/iwlwifi.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bluetooth.o: drivers/net/bluetooth.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/netstack.o: net/netstack.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/nic.o: drivers/net/nic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ip.o: net/ip.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ipv6.o: net/ipv6.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ipv6_transport.o: net/ipv6_transport.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ethernet.o: net/ethernet.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tcp_raw.o: net/tcp_raw.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/udp_raw.o: net/udp_raw.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/dns.o: net/dns.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/dhcp.o: net/dhcp.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/checksum.o: net/checksum.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/nat.o: net/nat.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/dns_server.o: net/dns_server.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/dhcp_server.o: net/dhcp_server.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/airmesh.o: net/airmesh.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/wireguard.o: net/wireguard.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/httpd.o: net/httpd.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/xhci.o: drivers/usb/xhci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/usb_ehci.o: drivers/usb/usb_ehci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/usb_ohci.o: drivers/usb/usb_ohci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/usb_msc.o: drivers/usb/usb_msc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/keyboard.o: drivers/char/keyboard.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mouse.o: drivers/char/mouse.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/touchpad.o: drivers/char/touchpad.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/printer.o: drivers/char/printer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/serial.o: drivers/char/serial.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/screen.o: drivers/char/screen.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pty.o: drivers/char/pty.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hdaudio.o: drivers/audio/hdaudio.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ac97.o: drivers/audio/ac97.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pcspeaker.o: drivers/audio/pcspeaker.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vbe.o: drivers/video/vbe.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/virtio_gpu.o: drivers/video/virtio_gpu.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/i915.o: drivers/video/i915.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gpu_drm.o: drivers/video/gpu_drm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gpu_backend.o: drivers/video/gpu_backend.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/display_mst.o: drivers/video/display_mst.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/string.o: kernel/lib/string.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/entropy.o: kernel/lib/entropy.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/spinlock.o: kernel/sync/spinlock.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rcu.o: kernel/sync/rcu.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/security.o: kernel/security/security.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/memory_protect.o: kernel/security/memory_protect.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/task_protect.o: kernel/security/task_protect.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/irq_sanitizer.o: kernel/security/irq_sanitizer.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/audit.o: kernel/security/audit.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/antivirus.o: kernel/monitor/antivirus.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ai_security.o: kernel/security/ai_security.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kshield.o: kernel/security/kshield.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/security_hook.o: kernel/security/security_hook.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/self_heal.o: kernel/lib/self_heal.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fde.o: kernel/security/fde.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sha256.o: crypto/sha256.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tpm2.o: kernel/security/tpm2.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/epoll.o: net/epoll.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/io_uring.o: kernel/tracing/io_uring.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/io_scheduler.o: fs/io_scheduler.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ebpf.o: kernel/tracing/ebpf.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kprobes.o: kernel/tracing/kprobes.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shm.o: kernel/ipc/shm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ipc.o: kernel/ipc/ipc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cgroup.o: kernel/security/cgroup.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/namespace.o: kernel/security/namespace.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sandbox.o: kernel/security/sandbox.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/resource_monitor.o: kernel/monitor/resource_monitor.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/resource_engine.o: kernel/monitor/resource_engine.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/process_monitor.o: kernel/monitor/process_monitor.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/memory_monitor.o: kernel/monitor/memory_monitor.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/io_guard.o: kernel/monitor/io_guard.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/threat_logger.o: kernel/monitor/threat_logger.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/paniclog.o: kernel/monitor/paniclog.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/signature_updater.o: kernel/monitor/signature_updater.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel_monitor.o: kernel/monitor/kernel_monitor.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lastmap.o: kernel/monitor/lastmap.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lastshark.o: kernel/monitor/lastshark.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/metrics.o: kernel/metrics.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sysinfo.o: kernel/monitor/sysinfo.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/persist.o: kernel/lib/persist.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/clipboard.o: kernel/lib/clipboard.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/secure_patch.o: kernel/lib/secure_patch.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ktrace.o: kernel/monitor/ktrace.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/module.o: kernel/lib/module.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kinit.o: kernel/lib/kinit.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/proc_core.o: kernel/ipc/proc_core.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/plugin.o: kernel/plugin.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/memlab.o: mm/memlab.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/schedlab.o: kernel/sched/schedlab.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/iolab.o: fs/iolab.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/seclab.o: kernel/security/seclab.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/compatlab.o: kernel/lib/compatlab.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/driverlab.o: drivers/bus/driverlab.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fusionlab.o: kernel/ipc/fusionlab.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rust_bridge.o: kernel/lib/rust_bridge.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/layers.o: kernel/lib/layers.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/levels.o: kernel/lib/levels.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pcoe2.o: kernel/lib/pcoe2.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pcoe2_core.o: kernel/lib/pcoe2_core.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pcoe2_matrix.o: kernel/lib/pcoe2_matrix.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/compat.o: kernel/lib/compat.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/compat_engine.o: kernel/lib/compat_engine.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/compat_astg.o: kernel/lib/compat_astg.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/compat_bbf.o: kernel/lib/compat_bbf.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/compat_mmul.o: kernel/lib/compat_mmul.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/compat_sece.o: kernel/lib/compat_sece.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/compat_dllsync.o: kernel/lib/compat_dllsync.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/compat_bridge.o: kernel/lib/compat_bridge.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/linux_compat.o: kernel/compat/linux_compat.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/linux_rootfs.o: kernel/compat/linux_rootfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/wayland_bridge.o: kernel/compat/wayland_bridge.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hal.o: kernel/lib/hal.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ultimate.o: kernel/lib/ultimate.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/universal_runtime.o: kernel/lib/universal_runtime.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sdc.o: kernel/lib/sdc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/selftest.o: kernel/monitor/selftest.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gfx.o: drivers/video/gfx.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/secure_io.o: drivers/char/secure_io.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/userdb.o: kernel/lib/userdb.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/elf_loader.o: kernel/lib/elf_loader.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tls_server.o: crypto/tls_server.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tls_identity.h: tools/gen_tls_identity.py | $(BUILD_DIR)
	python3 tools/gen_tls_identity.py $@

$(BUILD_DIR)/tls_identity.o: crypto/tls_identity.c $(BUILD_DIR)/tls_identity.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tls_bearssl.o: crypto/tls_bearssl.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bearssl_entropy.o: crypto/bearssl_entropy.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pkg_manager.o: kernel/lib/pkg_manager.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/controller.o: kernel/monitor/controller.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/banner.o: kernel/monitor/banner.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/connect_scan.o: kernel/monitor/connect_scan.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/multicore_dispatch.o: kernel/monitor/multicore_dispatch.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rate_limiter.o: kernel/monitor/rate_limiter.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/scan_table.o: kernel/monitor/scan_table.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syn_scan.o: kernel/monitor/syn_scan.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/timing_engine.o: kernel/monitor/timing_engine.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/udp_scan.o: kernel/monitor/udp_scan.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shell.o: shell/shell.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/commands.o: shell/commands.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shell_threatlog.o: shell/threatlog.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shell_processinfo.o: shell/processinfo.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shell_sysinfo_view.o: shell/sysinfo.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ------------------------------------------------------------------------------
# In-Kernel BearSSL Crypto Library
# ------------------------------------------------------------------------------
BEARSSL_SRCS := $(filter-out %/sysrng.c,$(shell find crypto/bearssl -name '*.c'))

$(BUILD_DIR)/libbearssl.a: $(BEARSSL_SRCS) | $(BUILD_DIR)
	@rm -rf $(BUILD_DIR)/bearssl_obj
	@mkdir -p $(BUILD_DIR)/bearssl_obj
	@for f in $(BEARSSL_SRCS); do \
		gcc -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -fno-pic -fno-stack-protector -fno-builtin \
			-fno-asynchronous-unwind-tables -I"$(CURDIR)/include/std" -I"$(CURDIR)/crypto/bearssl" -I"$(CURDIR)/include" \
			-DBR_USE_UNIX_TIME=0 -Os \
			-c "$$f" -o "$(BUILD_DIR)/bearssl_obj/$$(basename $$f .c).o" || exit 1; \
	done
	@rm -f $(BUILD_DIR)/libbearssl.a
	@ar rcs $(BUILD_DIR)/libbearssl.a $(BUILD_DIR)/bearssl_obj/*.o

# ------------------------------------------------------------------------------
# Linker Target
# ------------------------------------------------------------------------------
$(BUILD_DIR)/kernel.elf: $(ALL_OBJS) $(BUILD_DIR)/libbearssl.a
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS) $(BUILD_DIR)/libbearssl.a
	@echo ">>> [LastKernel] Pure 64-Bit ELF Kernel built successfully: $@"

# ------------------------------------------------------------------------------
# Bootable Standalone ISO
# ------------------------------------------------------------------------------
iso: $(BUILD_DIR)/lastkernel.iso

$(BUILD_DIR)/lastkernel.iso: $(BUILD_DIR)/kernel.elf
	rm -rf $(BUILD_DIR)/iso_root
	mkdir -p $(BUILD_DIR)/iso_root/boot/limine $(BUILD_DIR)/iso_root/EFI/BOOT
	cp $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/iso_root/boot/kernel.elf
	cp limine.conf $(BUILD_DIR)/iso_root/boot/limine/limine.conf
	cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin $(BUILD_DIR)/iso_root/boot/limine/
	cp limine/BOOTX64.EFI $(BUILD_DIR)/iso_root/EFI/BOOT/
	xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin -no-emul-boot \
		-boot-load-size 4 -boot-info-table --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(BUILD_DIR)/iso_root -o $@
	./limine/limine bios-install $@
	@echo ">>> [LastKernel] Standalone Pure Kernel ISO created: $@"

clean:
	rm -rf $(BUILD_DIR)

test:
	@echo ">>> [LastKernel] Running kernel test suite..."
	mkdir -p $(BUILD_DIR)/tests
	awk '/^\/\* ==================== AES-128 Implementation/,/^\/\* ==================== PBKDF2/' kernel/security/fde.c | head -n -2 > $(BUILD_DIR)/tests/fde_aes_core.inc
	gcc -O1 -Wall -Wextra -I$(BUILD_DIR)/tests -o $(BUILD_DIR)/tests/fde_aes_kat tests/fde_aes_kat.c
	./$(BUILD_DIR)/tests/fde_aes_kat
	gcc -O2 -Wall -Wextra -iquote include -o $(BUILD_DIR)/tests/lastfs_tests tests/lastfs_tests.c
	./$(BUILD_DIR)/tests/lastfs_tests

qemu: $(BUILD_DIR)/lastkernel.iso
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/lastkernel.iso -m 512M -serial stdio
