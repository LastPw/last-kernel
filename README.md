# LastKernel — Standalone 64-Bit Monolithic OS Kernel

**LastKernel** is a hobby 64-bit (`x86_64`) monolithic operating system kernel written in freestanding C with a small Rust component and assembly bootstrapping. It is decoupled from userland applications and graphical desktop environments.

> **Status honesty note:** This README distinguishes strictly between *implemented*, *experimental*, and *not yet implemented* features. Modules that previously claimed Wi-Fi, Bluetooth, Intel i915, HD Audio, EHCI, WireGuard, eBPF, kprobes, io_uring, or Wayland support without doing the underlying work have been **removed from the tree** (see history). What remains below is what actually ships.

---

## Implemented

### Boot & CPU
- Limine boot protocol (`limine.conf`), BIOS + UEFI, higher-half kernel.
- Long-mode entry, GDT/IDT, interrupt stubs, CPU feature detection, topology enumeration.
- SMP: AP trampoline, LAPIC init, per-CPU areas.

### Memory Management
- Four-level paging with Higher-Half Direct Map (HHDM).
- Buddy frame allocator (`mm/frame_allocator.c`), slab allocator (`mm/slab.c`), kernel heap (`mm/heap.c`).
- VMA manager (`mm/vma.c`), hash-LRU page cache with write-behind (`mm/page_cache.c`).
- OOM killer (`mm/oom.c`) and memory watchdog (`mm/watchdog.c`).

### Scheduling & Sync
- Preemptive SMP scheduler with quantum and multicore dispatchers (`kernel/sched/`).
- Kernel threads, waitqueues, spinlocks, RCU (`kernel/sync/`).

### Filesystems (VFS)
- VFS layer with dynamic mount table, devfs, pipes.
- **Ext2** (read/write, `fs/ext2.c`), **FAT** (full read/write: file create/delete, cluster-chain allocation with grow/shrink rewrites, subdirectories, disk-full handling — covered by host-side mock-disk vectors in `tests/fat_tests.c`; 8.3 short names only), Minix v1/v2 (`fs/minix.c`), NTFS read-only (`fs/ntfs.c`).
- **LastFS 2.0** (`fs/lastfs.c`): native FS with WAL journaling, extent allocation, CRC32C integrity — covered by host-side unit tests including crash/journal-replay recovery (`tests/lastfs_tests.c`).

### Drivers
- **Storage**: NVMe over PCIe, AHCI SATA, IDE ATA, VirtIO-block, USB mass storage.
- **USB**: xHCI (real TRB rings/event ring) and OHCI (real ED/TD chains + SCSI CBW/CSW).
- **NICs**: Intel e1000, Realtek RTL8139/RTL8169, AMD PCnet-FAST III, VirtIO-net.
- **Video**: VBE framebuffer, VirtIO-GPU, DRM-style backend layer.
- **Audio**: AC'97 (real codec I/O via NAM/NABM), PC speaker.
- **Input/char**: PS/2 keyboard & mouse, touchpad, serial COM, PTY, printer.

### Network Stack
- Ethernet II, ARP, IPv4, IPv6 (+ NDP), ICMP/ICMPv6.
- **WireGuard IP-layer tunneling**: IPv4 datagrams bound for the configured allowed network are automatically encapsulated into the tunnel at the frame-emission choke point; decapsulated peer datagrams are injected back into the stack. Allowed-network traffic is never leaked in the clear (oversized frames are dropped and counted).
- Basic TCP state machine (`net/tcp_raw.c`) and UDP.
- DHCP client + server, DNS resolver + server, NAT engine.
- In-kernel HTTP/TLS server on vendored **BearSSL** (`crypto/bearssl/`) with real embedded trust anchors.
- AirMesh (`net/airmesh.c`): *experimental* L2 beacon/text protocol over Ethernet — a toy, not mesh routing.

### Security & Crypto
- **In-kernel WireGuard v1** (`net/wireguard.c`, `crypto/noise_ik.c`): spec-exact Noise_IKpsk2 handshake (X25519 via BearSSL, HMAC-BLAKE2s KDF, ChaCha20-Poly1305 transport), TAI64N replay-resistant timestamps with flood guard, 2048-bit sliding replay window, retry/rekey/keepalive timers, MAC1 authentication. Covered by host-side vectors: RFC 7693 BLAKE2s KATs, RFC 7748 X25519 §6.1, full dual-role handshake exchange, tamper + wrong-key rejection (`tests/wg_noise_tests.c`).
- Full disk encryption: AES-XTS + PBKDF2 (`kernel/security/fde.c`), validated against known-answer test vectors via `make test`.
- SHA-256 and BLAKE2s (`crypto/blake2s.c`), vendored BearSSL for TLS.
- Stack canary protection, IRQ rate limiting, task pointer/context validation.
- cgroups, namespaces, sandboxing hooks; audit log; threat event ring buffer.
- Heuristic risk scoring (`kernel/security/ai_security.c`, `kernel/security/kshield.c`) — rule-based scoring over live telemetry, not machine learning.

### Userspace-ish
- ELF64 loader (`kernel/lib/elf_loader.c`).
- POSIX-flavored syscall layer + early Linux compatibility sandbox (`kernel/compat/linux_compat.c`).
- In-kernel diagnostic shell (`shell/`).

---

## Partial / Known Limitations
- **WireGuard**: no mac2/cookie replies under load, no roaming, single peer, 1400-byte inner MTU. End-to-end tunneling between two VMs is wired but not yet exercised in CI.
- **TCP** is minimal: no retransmission timers or sliding window yet.
- **TPM2** (`kernel/security/tpm2.c`): real SHA-256 seal/unseal logic, but no TIS hardware I/O — PCR values are software-derived.
- **Antivirus scanner**: real scanning/quarantine plumbing, toy signature set.
- **pcoe2 / hyper_ghost / ultimate** policy engines: real control plumbing over scheduler/power/memory knobs, but several "predicted" metrics are heuristic formulas, not measurements.
- **Linux compat**: syscall translation surface only; no dynamic-linker support.

## Not Implemented (roadmap)
- Robust TCP (retransmit, congestion control).
- eBPF, kprobes, io_uring (removed as non-functional stubs; to be reimplemented properly or not at all).
- ACPI power management, hotplug.

---

## Building LastKernel

### Prerequisites

On Linux (Ubuntu/Debian or Arch):
```bash
sudo apt update
sudo apt install -y build-essential gcc nasm xorriso python3 rustc
```

### Build Commands

1. **Compile kernel ELF binary**:
   ```bash
   make
   ```
   *Output*: `build/kernel.elf`

2. **Generate bootable ISO (BIOS + UEFI)**:
   ```bash
   make iso
   ```
   *Output*: `build/lastkernel.iso`

3. **Run self-tests & cryptographic vector validation**:
   ```bash
   make test
   ```

4. **Launch in QEMU**:
   ```bash
   make qemu
   ```

---

## Source Tree Structure

```
last-kernel/
├── arch/x86_64/         # CPU init, GDT, IDT, interrupts, SMP trampoline, paging
├── crypto/              # AES-XTS/SHA-256 FDE crypto + vendored BearSSL (TLS)
├── drivers/             # Storage (NVMe/AHCI/IDE/VirtIO), NICs, USB xHCI/OHCI, video, audio, char
├── fs/                  # VFS, mounts, Ext2, FAT32, Minix, NTFS (RO), LastFS 2.0
├── include/             # Standalone kernel headers
├── kernel/              # Core orchestration, scheduler, IPC, security, monitors
├── mm/                  # Buddy allocator, slab, VMA, heap, page cache, OOM
├── net/                 # IPv4/IPv6, TCP/UDP, DHCP/DNS client+server, NAT, httpd, airmesh (exp.)
├── rust_kernel/         # Rust kernel extensions
├── shell/               # In-kernel diagnostic CLI
├── tests/               # Host-side test vectors (FDE AES KATs, LastFS journaling/recovery)
├── tools/               # Build helpers (TLS identity generation)
├── linker.ld            # 64-bit higher-half linker script
├── limine.conf          # Limine bootloader configuration
└── Makefile             # Monolithic kernel build system
```
