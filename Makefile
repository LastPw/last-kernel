# ==============================================================================
# LastKernel — Pure Standalone 64-Bit Monolithic OS Kernel Makefile
# Architectures: x86_64 SMP
# ==============================================================================

CC      := gcc
AS      := nasm
LD      := ld
RUSTC   := rustc

CFLAGS  := -m64 -mcmodel=small -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -ffreestanding -fno-pic -fstack-protector-strong \
           -mstack-protector-guard=global -nostdlib -nostdinc \
           -Iinclude -Iinclude/common -Iinclude/kernel -Iinclude/drivers \
           -Iinclude/fs -Iinclude/net -Iinclude/mm -Iinclude/security \
           -Iinclude/crypto -Iinclude/apk -Iinclude/std \
           -Wall -Wextra -O2

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
	$(BUILD_DIR)/interrupt_stubs.o \
	$(BUILD_DIR)/pku.o

C_OBJS := \
	$(BUILD_DIR)/bootinfo.o \
	$(BUILD_DIR)/cmdline.o \
	$(BUILD_DIR)/kernel.o \
	$(BUILD_DIR)/stack_protector.o \
	$(BUILD_DIR)/core.o \
	$(BUILD_DIR)/vtty.o \
	$(BUILD_DIR)/ultimate_kernel.o \
	$(BUILD_DIR)/kthread.o \
	$(BUILD_DIR)/gdt.o \
	$(BUILD_DIR)/idt.o \
	$(BUILD_DIR)/interrupts.o \
	$(BUILD_DIR)/cpu_detect.o \
	$(BUILD_DIR)/hw_topology.o \
	$(BUILD_DIR)/smp.o \
	$(BUILD_DIR)/ap_worker.o \
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
	$(BUILD_DIR)/exfat.o \
	$(BUILD_DIR)/ntfs.o \
	$(BUILD_DIR)/minix.o \
	$(BUILD_DIR)/persistent_disk.o \
	$(BUILD_DIR)/lastfs.o \
	$(BUILD_DIR)/iso9660.o \
	$(BUILD_DIR)/file.o \
	$(BUILD_DIR)/e1000.o \
	$(BUILD_DIR)/rtl8169.o \
	$(BUILD_DIR)/rtl8139.o \
	$(BUILD_DIR)/pcnet.o \
	$(BUILD_DIR)/virtio_net.o \
	$(BUILD_DIR)/wifi.o \
	$(BUILD_DIR)/netstack.o \
	$(BUILD_DIR)/nic.o \
	$(BUILD_DIR)/ip.o \
	$(BUILD_DIR)/ipv6.o \
	$(BUILD_DIR)/ipv6_transport.o \
	$(BUILD_DIR)/ethernet.o \
	$(BUILD_DIR)/tcp_raw.o \
	$(BUILD_DIR)/tcp_window.o \
	$(BUILD_DIR)/udp_raw.o \
	$(BUILD_DIR)/dns.o \
	$(BUILD_DIR)/dhcp.o \
	$(BUILD_DIR)/checksum.o \
	$(BUILD_DIR)/nat.o \
	$(BUILD_DIR)/dns_server.o \
	$(BUILD_DIR)/dhcp_server.o \
	$(BUILD_DIR)/airmesh.o \
	$(BUILD_DIR)/wireguard.o \
	$(BUILD_DIR)/xhci.o \
	$(BUILD_DIR)/usb_ohci.o \
	$(BUILD_DIR)/usb_mass.o \
	$(BUILD_DIR)/keyboard.o \
	$(BUILD_DIR)/mouse.o \
	$(BUILD_DIR)/touchpad.o \
	$(BUILD_DIR)/printer.o \
	$(BUILD_DIR)/serial.o \
	$(BUILD_DIR)/screen.o \
	$(BUILD_DIR)/pty.o \
	$(BUILD_DIR)/hda.o \
	$(BUILD_DIR)/sound.o \
	$(BUILD_DIR)/headphone.o \
	$(BUILD_DIR)/microphone.o \
	$(BUILD_DIR)/ac97.o \
	$(BUILD_DIR)/pcspeaker.o \
	$(BUILD_DIR)/vbe.o \
	$(BUILD_DIR)/virtio_gpu.o \
	$(BUILD_DIR)/gpu_drm.o \
	$(BUILD_DIR)/gpu_backend.o \
	$(BUILD_DIR)/gpu_3d.o \
	$(BUILD_DIR)/intel_i915.o \
	$(BUILD_DIR)/amd_radeon.o \
	$(BUILD_DIR)/nvidia_nouveau.o \
	$(BUILD_DIR)/display_mst.o \
	$(BUILD_DIR)/bluetooth_core.o \
	$(BUILD_DIR)/bt_intel.o \
	$(BUILD_DIR)/bt_realtek.o \
	$(BUILD_DIR)/acpi_ec.o \
	$(BUILD_DIR)/battery_sbs.o \
	$(BUILD_DIR)/laptop_vendors.o \
	$(BUILD_DIR)/backlight_als.o \
	$(BUILD_DIR)/cpu_power.o \
	$(BUILD_DIR)/numa.o \
	$(BUILD_DIR)/raid_server.o \
	$(BUILD_DIR)/server_net.o \
	$(BUILD_DIR)/ipmi_bmc.o \
	$(BUILD_DIR)/microvm.o \
	$(BUILD_DIR)/server_profile.o \
	$(BUILD_DIR)/string.o \
	$(BUILD_DIR)/entropy.o \
	$(BUILD_DIR)/spinlock.o \
	$(BUILD_DIR)/rcu.o \
	$(BUILD_DIR)/security.o \
	$(BUILD_DIR)/kaslr.o \
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
	$(BUILD_DIR)/blake2s.o \
	$(BUILD_DIR)/noise_ik.o \
	$(BUILD_DIR)/tpm2.o \
	$(BUILD_DIR)/epoll.o \
	$(BUILD_DIR)/io_scheduler.o \
	$(BUILD_DIR)/io_uring.o \
	$(BUILD_DIR)/shm.o \
	$(BUILD_DIR)/ipc.o \
	$(BUILD_DIR)/cgroup.o \
	$(BUILD_DIR)/namespace.o \
	$(BUILD_DIR)/sandbox.o \
	$(BUILD_DIR)/resource_monitor.o \
	$(BUILD_DIR)/resource_engine.o \
	$(BUILD_DIR)/process_monitor.o \
	$(BUILD_DIR)/memory_monitor.o \
	$(BUILD_DIR)/paniclog.o \
	$(BUILD_DIR)/metrics.o \
	$(BUILD_DIR)/persist.o \
	$(BUILD_DIR)/secure_patch.o \
	$(BUILD_DIR)/ktrace.o \
	$(BUILD_DIR)/module.o \
	$(BUILD_DIR)/kinit.o \
	$(BUILD_DIR)/proc_core.o \
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
	$(BUILD_DIR)/hal.o \
	$(BUILD_DIR)/ultimate.o \
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
	$(BUILD_DIR)/apk.o \
	$(BUILD_DIR)/apk_core.o \
	$(BUILD_DIR)/apk_tx_journal.o \
	$(BUILD_DIR)/apk_guard.o \
	$(BUILD_DIR)/apk_sched_morph.o \
	$(BUILD_DIR)/apk_pe_loader.o \
	$(BUILD_DIR)/apk_wasm_engine.o \
	$(BUILD_DIR)/apk_domain_bridge.o \
	$(BUILD_DIR)/apk_ebpf.o \
	$(BUILD_DIR)/apk_ring.o \
	$(BUILD_DIR)/apk_ai_sched.o \
	$(BUILD_DIR)/apk_iommu.o \
	$(BUILD_DIR)/apk_wasm_jit.o \
	$(BUILD_DIR)/apk_fault_inject.o \
	$(BUILD_DIR)/kconsole.o

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

$(BUILD_DIR)/pku.o: arch/x86_64/boot/pku.asm | $(BUILD_DIR)
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

$(BUILD_DIR)/cmdline.o: kernel/cmdline.c | $(BUILD_DIR)
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

$(BUILD_DIR)/ap_worker.o: arch/x86_64/cpu/ap_worker.c | $(BUILD_DIR)
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

$(BUILD_DIR)/exfat.o: fs/exfat.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ntfs.o: fs/ntfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/minix.o: fs/minix.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/persistent_disk.o: fs/persistent_disk.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lastfs.o: fs/lastfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/iso9660.o: fs/iso9660.c | $(BUILD_DIR)
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

$(BUILD_DIR)/wifi.o: drivers/net/wifi.c | $(BUILD_DIR)
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

$(BUILD_DIR)/tcp_window.o: net/tcp_window.c | $(BUILD_DIR)
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

$(BUILD_DIR)/xhci.o: drivers/usb/xhci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/usb_ohci.o: drivers/usb/usb_ohci.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/usb_mass.o: drivers/usb/usb_mass.c | $(BUILD_DIR)
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

$(BUILD_DIR)/hda.o: drivers/audio/hda.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sound.o: drivers/audio/sound.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/headphone.o: drivers/audio/headphone.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/microphone.o: drivers/audio/microphone.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ac97.o: drivers/audio/ac97.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pcspeaker.o: drivers/audio/pcspeaker.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vbe.o: drivers/video/vbe.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/virtio_gpu.o: drivers/video/virtio_gpu.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gpu_drm.o: drivers/video/gpu_drm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gpu_backend.o: drivers/video/gpu_backend.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gpu_3d.o: drivers/video/gpu_3d.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/intel_i915.o: drivers/video/intel_i915.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/amd_radeon.o: drivers/video/amd_radeon.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/nvidia_nouveau.o: drivers/video/nvidia_nouveau.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/display_mst.o: drivers/video/display_mst.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bluetooth_core.o: drivers/bluetooth/bluetooth_core.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bt_intel.o: drivers/bluetooth/bt_intel.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bt_realtek.o: drivers/bluetooth/bt_realtek.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/acpi_ec.o: drivers/platform/acpi_ec.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/battery_sbs.o: drivers/platform/battery_sbs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/laptop_vendors.o: drivers/platform/laptop_vendors.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/backlight_als.o: drivers/platform/backlight_als.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cpu_power.o: drivers/platform/cpu_power.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/numa.o: kernel/server/numa.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/raid_server.o: drivers/block/raid_server.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/server_net.o: net/server_net.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ipmi_bmc.o: drivers/platform/ipmi_bmc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/microvm.o: kernel/server/microvm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/server_profile.o: kernel/server/server_profile.c | $(BUILD_DIR)
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

$(BUILD_DIR)/kaslr.o: kernel/security/kaslr.c | $(BUILD_DIR)
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

$(BUILD_DIR)/blake2s.o: crypto/blake2s.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/noise_ik.o: crypto/noise_ik.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tpm2.o: kernel/security/tpm2.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/epoll.o: net/epoll.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/io_scheduler.o: fs/io_scheduler.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/io_uring.o: kernel/io_uring.c | $(BUILD_DIR)
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

$(BUILD_DIR)/paniclog.o: kernel/monitor/paniclog.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/metrics.o: kernel/metrics.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/persist.o: kernel/lib/persist.c | $(BUILD_DIR)
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

$(BUILD_DIR)/apk.o: kernel/lib/apk.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apk_core.o: kernel/apk/apk_core.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apk_tx_journal.o: kernel/apk/apk_tx_journal.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apk_guard.o: kernel/apk/apk_guard.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apk_sched_morph.o: kernel/apk/apk_sched_morph.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apk_pe_loader.o: kernel/apk/apk_pe_loader.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apk_wasm_engine.o: kernel/apk/apk_wasm_engine.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apk_domain_bridge.o: kernel/apk/apk_domain_bridge.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apk_ebpf.o: kernel/apk/apk_ebpf.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apk_ring.o: kernel/apk/apk_ring.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apk_ai_sched.o: kernel/apk/apk_ai_sched.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apk_iommu.o: kernel/apk/apk_iommu.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apk_wasm_jit.o: kernel/apk/apk_wasm_jit.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/apk_fault_inject.o: kernel/apk/apk_fault_inject.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hal.o: kernel/lib/hal.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ultimate.o: kernel/lib/ultimate.c | $(BUILD_DIR)
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

$(BUILD_DIR)/kconsole.o: kernel/kconsole.c | $(BUILD_DIR)
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
			-fno-asynchronous-unwind-tables -I"$(CURDIR)/include/std" -I"$(CURDIR)/crypto/bearssl" -I"$(CURDIR)/include" -I"$(CURDIR)/include/crypto" \
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
# In-House Native Bootloader (Pulse Loader)
# ------------------------------------------------------------------------------
$(BUILD_DIR)/BOOTX64.EFI $(BUILD_DIR)/BOOTIA32.EFI: | $(BUILD_DIR)
	@echo ">>> [LastKernel] Building native Pulse Loader (UEFI)..."
	@$(MAKE) -C bootloader/pulse build/PULSELOADER-x64.EFI build/PULSELOADER-ia32.EFI
	@cp bootloader/pulse/build/PULSELOADER-x64.EFI $(BUILD_DIR)/BOOTX64.EFI
	@cp bootloader/pulse/build/PULSELOADER-ia32.EFI $(BUILD_DIR)/BOOTIA32.EFI

# ------------------------------------------------------------------------------
# Bootable Standalone Disk Image (FAT32 with Native Pulse Loader)
# ------------------------------------------------------------------------------
image: $(BUILD_DIR)/lastkernel.img
iso: $(BUILD_DIR)/lastkernel.img

$(BUILD_DIR)/lastkernel.img: $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/BOOTX64.EFI $(BUILD_DIR)/BOOTIA32.EFI
	@rm -f $@
	@mformat -C -i $@ -f 2880 ::
	@mmd -i $@ ::/EFI ::/EFI/BOOT ::/boot
	@mcopy -i $@ $(BUILD_DIR)/BOOTX64.EFI  ::/EFI/BOOT/BOOTX64.EFI
	@mcopy -i $@ $(BUILD_DIR)/BOOTIA32.EFI ::/EFI/BOOT/BOOTIA32.EFI
	@printf 'timeout=3\ndefault=0\n\n[entry LastKernel]\nkernel=/boot/kernel.elf\ncmdline=BOOT_MODE=default\n' > $(BUILD_DIR)/pulse.conf
	@mcopy -i $@ $(BUILD_DIR)/pulse.conf ::/boot/pulse.conf
	@mcopy -i $@ $(BUILD_DIR)/kernel.elf ::/boot/kernel.elf
	@echo ">>> [LastKernel] Bootable disk image created with in-house Pulse Loader: $@"

clean:
	rm -rf $(BUILD_DIR) user/build
	@$(MAKE) -C bootloader/pulse clean

TEST_INCLUDES := -iquote include -iquote include/common -iquote include/kernel -iquote include/drivers \
                 -iquote include/fs -iquote include/net -iquote include/mm -iquote include/security \
                 -iquote include/crypto -iquote include/apk -iquote include/std -iquote include/storage

test:
	@echo ">>> [LastKernel] Running native Pulse Loader test matrix..."
	@$(MAKE) -C bootloader/pulse test
	@echo ">>> [LastKernel] Running kernel test suite..."
	mkdir -p $(BUILD_DIR)/tests
	awk '/^\/\* ==================== AES-128 Implementation/,/^\/\* ==================== PBKDF2/' kernel/security/fde.c | head -n -2 > $(BUILD_DIR)/tests/fde_aes_core.inc
	gcc -O1 -Wall -Wextra $(TEST_INCLUDES) -I$(BUILD_DIR)/tests -o $(BUILD_DIR)/tests/fde_aes_kat tests/fde_aes_kat.c
	./$(BUILD_DIR)/tests/fde_aes_kat
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/lastfs_tests tests/lastfs_tests.c
	./$(BUILD_DIR)/tests/lastfs_tests
	@mkdir -p $(BUILD_DIR)/tests/bearssl_obj
	@for f in $(BEARSSL_SRCS); do \
		gcc -O2 -w $(TEST_INCLUDES) -I"$(CURDIR)/crypto/bearssl" \
			-c "$$f" -o "$(BUILD_DIR)/tests/bearssl_obj/$$(basename $$f .c).o" || exit 1; \
	done
	ar rcs $(BUILD_DIR)/tests/libbearssl_host.a $(BUILD_DIR)/tests/bearssl_obj/*.o
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -Icrypto/bearssl -o $(BUILD_DIR)/tests/wg_noise_tests \
		tests/wg_noise_tests.c crypto/blake2s.c crypto/noise_ik.c $(BUILD_DIR)/tests/libbearssl_host.a
	./$(BUILD_DIR)/tests/wg_noise_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/fat_tests tests/fat_tests.c
	./$(BUILD_DIR)/tests/fat_tests
	gcc -O2 -w $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/iso_tests tests/iso_tests.c
	./$(BUILD_DIR)/tests/iso_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/tcp_window_tests tests/tcp_window_tests.c net/tcp_window.c
	./$(BUILD_DIR)/tests/tcp_window_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/apk_core_tests tests/apk_core_tests.c
	./$(BUILD_DIR)/tests/apk_core_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/apk_deep_tests tests/apk_deep_tests.c
	./$(BUILD_DIR)/tests/apk_deep_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/apk_nextgen_tests tests/apk_nextgen_tests.c
	./$(BUILD_DIR)/tests/apk_nextgen_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/apk_advanced_tests tests/apk_advanced_tests.c
	./$(BUILD_DIR)/tests/apk_advanced_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/hda_tests tests/hda_tests.c
	./$(BUILD_DIR)/tests/hda_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/usb_mass_tests tests/usb_mass_tests.c
	./$(BUILD_DIR)/tests/usb_mass_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/wifi_tests tests/wifi_tests.c drivers/net/wifi.c
	./$(BUILD_DIR)/tests/wifi_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/cmdline_tests tests/cmdline_tests.c
	./$(BUILD_DIR)/tests/cmdline_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/exfat_tests tests/exfat_tests.c fs/exfat.c
	./$(BUILD_DIR)/tests/exfat_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/ahci_tests tests/ahci_tests.c
	./$(BUILD_DIR)/tests/ahci_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/headphone_tests tests/headphone_tests.c drivers/audio/headphone.c
	./$(BUILD_DIR)/tests/headphone_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/microphone_tests tests/microphone_tests.c drivers/audio/microphone.c
	./$(BUILD_DIR)/tests/microphone_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/gpu_3d_tests tests/gpu_3d_tests.c drivers/video/gpu_3d.c
	./$(BUILD_DIR)/tests/gpu_3d_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/gpu_hw_drivers_tests tests/gpu_hw_drivers_tests.c drivers/video/intel_i915.c drivers/video/amd_radeon.c drivers/video/nvidia_nouveau.c
	./$(BUILD_DIR)/tests/gpu_hw_drivers_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/io_uring_tests tests/io_uring_tests.c kernel/io_uring.c
	./$(BUILD_DIR)/tests/io_uring_tests
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/bluetooth_tests tests/bluetooth_tests.c drivers/bluetooth/bluetooth_core.c drivers/bluetooth/bt_intel.c drivers/bluetooth/bt_realtek.c
	./$(BUILD_DIR)/tests/bluetooth_tests
	gcc -O2 -Wall -Wextra -DHOST_TEST $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/laptop_platform_tests tests/laptop_platform_tests.c drivers/platform/acpi_ec.c drivers/platform/battery_sbs.c drivers/platform/laptop_vendors.c drivers/platform/backlight_als.c drivers/platform/cpu_power.c
	./$(BUILD_DIR)/tests/laptop_platform_tests
	gcc -O2 -Wall -Wextra -DHOST_TEST $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/server_edition_tests tests/server_edition_tests.c kernel/server/numa.c drivers/block/raid_server.c net/server_net.c drivers/platform/ipmi_bmc.c kernel/server/microvm.c kernel/server/server_profile.c
	./$(BUILD_DIR)/tests/server_edition_tests
	gcc -O2 -Wall -Wextra -DHOST_TEST $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/installer_tests tests/installer_tests.c tools/installer/gpt_engine.c tools/installer/fs_formatter.c tools/installer/raid_engine.c tools/installer/crypto_engine.c tools/installer/net_engine.c tools/installer/hw_diagnostics.c tools/installer/bundle_deployer.c tools/installer/tui_engine.c tools/installer/installer_core.c
	./$(BUILD_DIR)/tests/installer_tests
	gcc -O2 -Wall -Wextra -DHOST_TEST $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/storage_protocol_tests tests/storage_protocol_tests.c storage/xdr_rpc.c storage/nfs4_server.c storage/smb3_server.c storage/iscsi_target.c storage/storage_manager.c
	./$(BUILD_DIR)/tests/storage_protocol_tests
	gcc -O2 -Wall -Wextra -DHOST_TEST $(TEST_INCLUDES) -o $(BUILD_DIR)/tests/vtty_tests tests/vtty_tests.c kernel/vtty.c
	./$(BUILD_DIR)/tests/vtty_tests
	@echo "\n======================================================="
	@echo "   LastHTTPD Enterprise Rust Web Server & WAF Tests    "
	@echo "======================================================="
	cd services/last-httpd && cargo test --quiet
	@echo ">>> ALL 14 LASTHTTPD ENTERPRISE SUITES PASSED 100% SUCCESSFULLY! <<<\n"

installer:
	@mkdir -p $(BUILD_DIR)
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/last-install tools/installer/last_install.c tools/installer/gpt_engine.c tools/installer/fs_formatter.c tools/installer/raid_engine.c tools/installer/crypto_engine.c tools/installer/net_engine.c tools/installer/hw_diagnostics.c tools/installer/bundle_deployer.c tools/installer/tui_engine.c tools/installer/installer_core.c
	@echo ">>> [LastKernel-Installer] Commercial CLI installer built: $(BUILD_DIR)/last-install"

storage:
	@mkdir -p $(BUILD_DIR)
	gcc -O2 -Wall -Wextra $(TEST_INCLUDES) -o $(BUILD_DIR)/last-storage tools/storage/last_storage.c storage/xdr_rpc.c storage/nfs4_server.c storage/smb3_server.c storage/iscsi_target.c storage/storage_manager.c
	@echo ">>> [LastKernel-Storage] Multi-Protocol Storage Manager built: $(BUILD_DIR)/last-storage"

server-services: installer storage
	@echo ">>> Building LastHTTPD Enterprise Server Daemon..."
	cd services/last-httpd && cargo build --release
	@mkdir -p $(BUILD_DIR)/server_root/conf $(BUILD_DIR)/server_root/public
	@cp services/last-httpd/conf/last-httpd.conf $(BUILD_DIR)/server_root/conf/
	@cp -r services/last-httpd/public/* $(BUILD_DIR)/server_root/public/
	@cp services/last-httpd/target/release/last-httpd $(BUILD_DIR)/httpd
	@echo ">>> LastHTTPD Enterprise Server ready: $(BUILD_DIR)/httpd"

server-image: $(BUILD_DIR)/kernel.elf server-services
	@echo ">>> [LastKernel-Server] Enterprise Server Edition Bootable Image ready: $(BUILD_DIR)/lastkernel.img"

OVMF_CODE := /usr/share/edk2/ovmf/OVMF_CODE.fd

qemu: $(BUILD_DIR)/lastkernel.img
	@if [ -f $(OVMF_CODE) ]; then \
		qemu-system-x86_64 -bios $(OVMF_CODE) -drive file=$(BUILD_DIR)/lastkernel.img,format=raw,if=ide -m 512M -serial stdio -display none; \
	else \
		qemu-system-x86_64 -drive file=$(BUILD_DIR)/lastkernel.img,format=raw -m 512M -serial stdio; \
	fi
