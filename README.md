# LastKernel — Standalone 64-Bit Monolithic OS Kernel

**LastKernel** is a pure, independent, 64-bit (`x86_64`) monolithic operating system kernel engineered in freestanding C and Rust with assembly bootstrapping. It is completely separated and decoupled from userland applications, graphical desktop shells, window managers, and userspace GUI suites.

---

## 🌟 Architecture Highlights

- **Architecture**: `x86_64` (AMD64 / Intel 64) Long Mode with SMP Multicore APIC/HPET Support.
- **Boot Protocol**: Modern 64-bit Limine Boot Protocol (`limine.conf`) supporting BIOS & UEFI boot.
- **Memory Architecture**:
  - Four-level Paging (`PML4` / `PDPT` / `PD` / `PT`) with Higher-Half Direct Map (HHDM).
  - Buddy Frame Allocator (`mm/frame_allocator.c`) & Slab Allocator (`mm/slab.c`).
  - Virtual Memory Areas (VMA) Manager (`mm/vma.c`).
  - Out-Of-Memory (OOM) Killer & Watchdog (`mm/oom.c`, `mm/watchdog.c`).
  - High-performance Hash-LRU Page Cache with write-behind dirty syncing (`mm/page_cache.c`).
  - Kernel Dynamic Heap (`mm/heap.c`).
- **Preemptive Multitasking & Scheduler**:
  - SMP Preemptive Task Scheduler (`kernel/sched/scheduler.c`).
  - Quantum Scheduler & Multicore Dispatcher (`kernel/sched/quantum_scheduler.c`, `kernel/sched/multicore_scheduler.c`).
  - Kernel Threads (`kthread.c`), Waitqueues (`wait.c`), Mutexes & Spinlocks (`kernel/sync/`).
- **Filesystem Subsystem (VFS)**:
  - Virtual Filesystem layer (`fs/vfs.c`) with dynamic mounting (`fs/mount.c`).
  - Native filesystem drivers: Ext2 (`fs/ext2.c`), FAT32 (`fs/fat.c`), Minix v1/v2 (`fs/minix.c`), NTFS Read-Only (`fs/ntfs.c`), LastFS (`fs/lastfs.c`), Persistent Disk (`fs/persistent_disk.c`).
  - Device Filesystem (`fs/devfs.c`) & UNIX Pipes (`fs/pipe.c`).
  - Asynchronous I/O Scheduler (`fs/io_scheduler.c`).
- **Comprehensive Hardware Device Drivers**:
  - **Storage Controllers**: NVMe PCIe (`drivers/block/nvme.c`), AHCI SATA (`drivers/block/ahci.c`), IDE ATA (`drivers/block/ide.c`), VirtIO-Block (`drivers/block/virtio_blk.c`), USB Mass Storage (`drivers/usb/usb_msc.c`).
  - **Network Interface Cards**: Intel Gigabit `e1000`/`e1000e` (`drivers/net/e1000.c`), Realtek `RTL8169` Gigabit (`drivers/net/rtl8169.c`), Realtek `RTL8139` (`drivers/net/rtl8139.c`), AMD PCnet-FAST III (`drivers/net/pcnet.c`), VirtIO-Net (`drivers/net/virtio_net.c`), Intel `iwlwifi` (`drivers/net/iwlwifi.c`), Bluetooth HCI (`drivers/net/bluetooth.c`).
  - **USB Host Controllers**: xHCI USB 3.0 (`drivers/usb/xhci.c`), EHCI USB 2.0 (`drivers/usb/usb_ehci.c`), OHCI USB 1.1 (`drivers/usb/usb_ohci.c`).
  - **Video & Graphics Drivers**: VBE Framebuffer (`drivers/video/vbe.c`), VirtIO-GPU 2D/3D with double-buffering (`drivers/video/virtio_gpu.c`), Intel HD/Iris Graphics (`drivers/video/i915.c`), DRM Graphics Layer (`drivers/video/gpu_drm.c`, `drivers/video/gpu_backend.c`).
  - **Audio Controllers**: Intel High Definition Audio (`drivers/audio/hdaudio.c`), AC'97 Audio (`drivers/audio/ac97.c`), PC Speaker (`drivers/audio/pcspeaker.c`).
  - **Character & Input Devices**: PS/2 Keyboard & Mouse (`drivers/char/keyboard.c`, `drivers/char/mouse.c`), Touchpad, Serial COM ports (`drivers/char/serial.c`), Virtual PTY (`drivers/char/pty.c`), Printer.
- **Enterprise Network Protocol Stack**:
  - Ethernet II, ARP, IPv4, IPv6, ICMP, ICMPv6 (`net/`).
  - Raw TCP state machine & UDP transport (`net/tcp_raw.c`, `net/udp_raw.c`).
  - DHCP Client & Built-in DHCP Server (`net/dhcp.c`, `net/dhcp_server.c`).
  - DNS Resolver & Built-in DNS Server (`net/dns.c`, `net/dns_server.c`).
  - NAT (Network Address Translation) Engine (`net/nat.c`).
  - Built-in In-Kernel WireGuard VPN Protocol (`net/wireguard.c`).
  - Mesh Networking Protocol (`net/airmesh.c`).
  - Embedded In-Kernel HTTPS / TLS 1.2/1.3 Web Server (`net/httpd.c`, `crypto/tls_server.c`).
- **Security & Integrity Shield**:
  - Full Disk Encryption (FDE) with AES-XTS and PBKDF2 (`kernel/security/fde.c`).
  - TPM 2.0 Hardware Security Module support (`kernel/security/tpm2.c`).
  - Stack Canary Protection (`kernel/stack_protector.c`).
  - Process Isolation & Sandboxing (`kernel/security/sandbox.c`, `kernel/security/cgroup.c`, `kernel/security/namespace.c`).
  - Real-time Threat Logger, Antivirus Scanner & Panic Log (`kernel/monitor/`).
  - In-Kernel eBPF VM (`kernel/tracing/ebpf.c`) and Dynamic Kprobes (`kernel/tracing/kprobes.c`).
  - Asynchronous `io_uring` ring buffer interface (`kernel/tracing/io_uring.c`).
- **Binary & Execution Formats**:
  - 64-bit ELF Executable Loader (`kernel/lib/elf_loader.c`).
  - POSIX & Linux Syscall Compatibility Layer (`kernel/syscall/syscall.c`, `kernel/compat/linux_compat.c`).

---

## 🛠️ Building LastKernel

### Prerequisites

On Linux (Ubuntu/Debian or Arch):
```bash
sudo apt update
sudo apt install -y build-essential gcc nasm xorriso python3 rustc
```

### Build Commands

1. **Compile Kernel ELF Binary**:
   ```bash
   make
   ```
   *Output*: `build/kernel.elf`

2. **Generate Bootable ISO (BIOS + UEFI)**:
   ```bash
   make iso
   ```
   *Output*: `build/lastkernel.iso`

3. **Run Self-Tests & Cryptographic Vector Validation**:
   ```bash
   make test
   ```

4. **Launch in QEMU**:
   ```bash
   make qemu
   ```

---

## 📁 Source Tree Structure

```
last-kernel/
├── arch/x86_64/         # CPU initialization, GDT, IDT, interrupts, SMP trampoline, paging
├── crypto/              # Cryptographic algorithms (AES, SHA256, TLS, BearSSL)
├── drivers/             # Storage, NIC, USB, Video/GPU, Audio, and Character drivers
├── fs/                  # Virtual File System, Mount table, Ext2, FAT32, Minix, NTFS, LastFS
├── include/             # Standalone kernel headers and data structures
├── kernel/              # Core kernel orchestration, scheduler, IPC, security, monitors
├── mm/                  # Buddy allocator, Slab, VMA, Heap, OOM, Page cache
├── net/                 # Network stack: IPv4/IPv6, TCP, UDP, WireGuard, DHCP, DNS, HTTP/TLS
├── rust_kernel/         # Safe Rust kernel extensions
├── shell/               # In-kernel diagnostic & debugging CLI shell
├── tests/               # Kernel test vectors (FDE AES, cryptographic roundtrips)
├── linker.ld            # 64-bit Higher-Half Kernel Linker Script
├── limine.conf          # Limine Bootloader Configuration
└── Makefile             # Pure Monolithic Kernel Build System
```
