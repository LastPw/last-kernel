# LastKernel

<div align="center">

```
 _                 _   _  __                    _ 
| |   __ _ ___ ___| |_| |/ /___ _ __ _ __   ___| |
| |  / _` / __/ __| __| ' // _ \ '__| '_ \ / _ \ |
| | | (_| \__ \__ \ |_| . \  __/ |  | | | |  __/ |
|_|  \__,_|___/___/\__|_|\_\___|_|  |_| |_|\___|_|
```

### Pure Standalone 64-Bit Monolithic Operating System Kernel & Native Drivers
*Designed and engineered in C, Assembly, and Rust for x86_64 SMP architectures.*

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg?style=flat-square)]()
[![Architecture](https://img.shields.io/badge/arch-x86__64%20SMP-blue.svg?style=flat-square)]()
[![Bootloader](https://img.shields.io/badge/bootloader-Pulse%20Loader%20(UEFI%2FBIOS)-orange.svg?style=flat-square)]()
[![Test Suite](https://img.shields.io/badge/tests-17%20suites%20passing%20100%25-success.svg?style=flat-square)]()
[![Lines of Code](https://img.shields.io/badge/code-186k%2B%20lines-informational.svg?style=flat-square)]()
[![Language](https://img.shields.io/badge/language-C11%20%7C%20NASM%20%7C%20Rust-lightblue.svg?style=flat-square)]()

</div>

---

## 📖 Overview

**LastKernel** is an industrial-grade, freestanding, pure 64-bit monolithic operating system kernel engineered from the ground up for modern `x86_64` PC hardware, workstations, and laptops. 

It is completely self-contained with its own native **Pulse Loader** (supporting both modern UEFI PE32+ and legacy BIOS multitrack booting), hardware graphics pipeline, complete audio digital signal processing (DSP) engine, high-speed asynchronous `io_uring` engine, Bluetooth 5.3 stack, ACPI embedded controller power governor, full TCP/IP networking, and hardware disk encryption.

---

## ⚡ Key Architectural Features

### 🖥️ 1. Core Kernel & SMP Execution Engine
* **Pure 64-Bit Long Mode**: Direct transition with Higher-Half Direct Map (`0xFFFF800000000000`), 4-level paging (PML4, PDPT, PD, PT), and 2MB/1GB huge pages.
* **Symmetric Multiprocessing (SMP)**: Multi-core AP trampoline with LAPIC timer calibration, I/O APIC routing, and lock-free Read-Copy-Update (RCU) synchronization.
* **Fair Quantum & Multicore Scheduler**: Preemptive task scheduling, POSIX-style thread groups, waitqueues, cgroups v2 resource controllers, and process isolation namespaces.
* **Rust Subsystem Integration**: Safe in-kernel memory and policy verification modules linked via static archive (`librust_kernel.a`).
* **High-Throughput `io_uring`**: Asynchronous I/O subsystem with lock-free SQ/CQ ring buffers, pre-registered zero-copy fixed memory buffers, and vectorized batch submission.

### 🎮 2. GPU Hardware Drivers & 3D Acceleration Engine
* **Industrial GPU Hardware Drivers**:
  * **Intel Iris / UHD Graphics (`i915`)**: Ring buffer command submission, PPGTT translation, MSI-X 8-vector interrupts, and automatic engine reset watchdog.
  * **AMD Radeon (`radeon`)**: GDS/CP ring dispatch, SRBM hardware watchdog, TDR recovery, and multi-display pipe management.
  * **NVIDIA GeForce (`nouveau`)**: FIFO pushbuffer, PFIFO/PGRAPH context switching, PMC auto-watchdog, and DMA bitmap memory recycling.
* **In-Kernel Direct Rendering Infrastructure (DRI)**: DRM/KMS IOCTL interface and LastGL execution pipeline.
* **3D Software Rasterizer Engine**:
  * 16.16 fixed-point sub-pixel barycentric triangle rasterizer.
  * Early-Z depth buffer testing and sub-pixel edge functions.
  * 8-light Blinn-Phong specular shading and bilinear texture mapping unit (TMU).
  * Utah Teapot bicubic Bézier patches and UV parametric sphere tessellator.
* **Display Multi-Stream Transport (MST)**: DisplayPort 1.4 topology management and virtual framebuffer compositing.

### 🎧 3. Audiophile Studio Audio & Voice DSP Engine
* **Intel HD Audio (HDA) & AC'97**: CORB/RIRB DMA command rings, multi-stream DMA playback/recording, and Realtek ALC887/ALC892 codec auto-configuration.
* **Audiophile Headphone Subsystem**:
  * Hardware impedance auto-sensing (16Ω to 600Ω consumer & audiophile dynamic rails).
  * 10-Band parametric Harman Target frequency equalization curve.
  * Bauer stereophonic crossfeed filter for reduced headphone listening fatigue.
  * Binaural 3D spatial audio with Woodworth Interaural Time Difference (ITD) vector positioning.
  * Studio lookahead peak limiter and WHO-compliant hearing protection (< 85 dBA clamp).
* **Broadcast Studio Microphone Subsystem**:
  * Real-time 48kHz 24-bit ADC audio streaming via lock-free ring buffer DMA.
  * Tri-factor Voice Activity Detection (VAD: Energy, Zero-Crossing Rate, Spectral Flatness).
  * Spectral noise suppression filter and adaptive spectral floor gating.
  * Butterworth 80Hz rumble high-pass filter and dynamic vocal de-esser.
  * Native RIFF/WAVE audio capture and PCM exporter.

### 💾 4. Storage Subsystem & Industrial Filesystems
* **Hardware Storage Controllers**:
  * **NVMe over PCIe**: Submission/Completion Queue (SQ/CQ) doorbell registers, 64-bit PRPs, and scatter-gather DMA.
  * **SATA-3 AHCI**: 16-byte PRDT descriptors, 48-bit LBA DMA EXT command FIS (0x27) packing, and word-swapped ATA IDENTIFY decoder.
  * **USB Mass Storage (BOT/SCSI)**: xHCI/OHCI TRB rings, Command Block Wrappers (CBW), and Command Status Wrappers (CSW).
* **Industrial Filesystem Implementations**:
  * **Microsoft exFAT**: 512B-4096B VBR, 16-bit cyclic directory set checksum, 16-bit upcase filename hash, multi-cluster allocation bitmap, and dynamic 64-bit file streams (>4GB).
  * **LastFS 2.0**: Native journaled filesystem with Write-Ahead Logging (WAL), extent trees, and CRC32C metadata protection.
  * **FAT16 / FAT32**: Full read/write support with directory cluster allocation and file shrink/expand.
  * **ISO9660**: CD/DVD-ROM filesystem with Rock Ridge extensions.
  * **Ext2 / Minix**: Linux second extended filesystem and Minix v1/v2 VFS integration.
* **Full Disk Encryption (FDE)**: In-kernel XTS-AES-256 sector encryption engine with PBKDF2-HMAC key derivation and anti-tamper verifiers.

### 📶 5. Networking, Wireless & Bluetooth 5.3 Stack
* **Complete Network Stack**:
  * Ethernet II, ARP, IPv4, IPv6 with Neighbor Discovery Protocol (NDP).
  * TCP engine with sliding window, cumulative ACK tracking, dynamic window scaling, and retransmission timeout (RTO).
  * UDP, ICMP / ICMPv6, DHCP client & server, DNS resolver & caching server, and NAT packet rewriting.
* **Wi-Fi 802.11 Stack**: Beacon frame parsing, Probe Request/Response, WPA2/WPA3 4-Way Handshake state machine, and CCMP-AES encryption.
* **Hardware NIC Drivers**: Intel E1000/E1000e, Realtek RTL8139/RTL8169, AMD PCnet-FAST III, VirtIO-Net.
* **Bluetooth 5.3 Core & Hardware Drivers**:
  * Host Controller Interface (HCI) packet processor and BLE active/passive scanner.
  * L2CAP multi-channel multiplexing with Segmentation and Reassembly (SAR).
  * In-kernel ATT/GATT server with standard GATT services (Generic Access, Battery, Device Info).
  * A2DP SBC 44.1kHz wireless stereo audio stream packetizer for Bluetooth headphones.
  * **Intel Bluetooth**: Support for Intel AC 7260/8260/9260/9560 and Wi-Fi 6/6E AX200/AX201/AX210/AX411 with secure TLV firmware patch loader.
  * **Realtek Bluetooth**: Support for RTL8723B/D/E, RTL8821CE, RTL8822CE, RTL8852AE/BE with ROM patch downloader.
* **WireGuard VPN & BearSSL TLS**:
  * In-kernel WireGuard v1 Noise_IKpsk2 tunnel with ChaCha20-Poly1305, BLAKE2s, and anti-replay sliding window.
  * Embedded BearSSL TLS 1.2/1.3 cryptographic engine with root certificate trust anchors.

### 🔋 6. Laptop Platform & Power Management Architecture
* **ACPI Embedded Controller (EC)**: Direct I/O ports `0x62`/`0x66`, Burst Mode transactions, and ACPI query event (`QR_EC`) queue.
* **Smart Battery System (SBS 1.1)**: State of Charge (SoC), voltage/current telemetry, battery health percentage, cycle counting, and remaining time estimator.
* **Multi-Vendor Platform Control**:
  * **Lenovo ThinkPad**: TrackPoint sensitivity (0-255), ThinkLight illumination, dual battery switching, and manual fan speed modes (Silent, Balanced, Performance, Turbo, Disengaged).
  * **ASUS ROG / ZenBook**: Aura RGB keyboard lighting (Rainbow, Breathing, Strobe) and Armoury Crate fan modes.
  * **Dell XPS / Latitude & Apple MacBook (SMC)**: Temperature sensors (`TC0P`), fan speed controllers (`F0Tg`), and SMBIOS power profiles.
* **Display PWM Backlight & ALS**: Logarithmic PWM brightness curve with smooth eye-comfort fade and Ambient Light Sensor (ALS) auto-dimming.
* **CPU DVFS Power Governor**: Intel SpeedStep, Intel HWP (Hardware P-States), AMD P-State, Energy-Performance Preference (EPP) levels, and turbo boost control up to 4.8 GHz.

---

## 📂 Codebase Directory Layout

```text
last-kernel/
├── bootloader/pulse/     # Native in-house UEFI/BIOS Pulse Bootloader
├── include/              # Clean modular categorized headers
│   ├── apk/              # APK transaction journal, wasm, and eBPF headers
│   ├── common/           # Fundamental types, string, spinlock, rcu, entropy
│   ├── crypto/           # BearSSL, SHA256, BLAKE2s, Noise-IK, TLS
│   ├── drivers/          # GPU (i915/Radeon/Nouveau), HDA, Bluetooth, Laptop Platform
│   ├── fs/               # exFAT, LastFS, FAT, ISO9660, Ext2, VFS
│   ├── kernel/           # Task, Scheduler, APIC, SMP, Timer, io_uring, Syscall
│   ├── mm/               # Paging, Frame Allocator, Slab, Heap, Page Cache, OOM
│   ├── net/              # Netstack, TCP Window, WireGuard, Wi-Fi, DHCP, DNS
│   ├── security/         # Sandbox, cgroups, FDE, kshield, TPM2, Audit
│   └── std/              # Standard C compatibility definitions
├── arch/x86_64/          # Assembly entrypoints, GDT/IDT, AP trampoline, PKU
├── kernel/               # Kernel core, schedulers, sync primitives, syscalls
├── drivers/              # Hardware driver implementations
│   ├── audio/            # HDA, Headphone DSP, Microphone DSP, AC97, PC Speaker
│   ├── block/            # NVMe, SATA AHCI, IDE, RAID, VirtIO-Block
│   ├── bluetooth/        # Bluetooth 5.3 Core, Intel AX210, Realtek RTL8852
│   ├── bus/              # PCI / PCIe MMCONFIG subsystem
│   ├── char/             # Keyboard, Mouse, Touchpad, Serial, Screen, PTY
│   ├── net/              # Intel E1000, Realtek RTL8139/8169, Wi-Fi 802.11
│   ├── platform/         # ACPI EC, Smart Battery SBS, ThinkPad/ASUS/SMC, Backlight, CPU DVFS
│   ├── usb/              # xHCI, OHCI, USB Mass Storage (BOT/SCSI)
│   └── video/            # Intel i915, AMD Radeon, NVIDIA Nouveau, 3D Rasterizer, DRI
├── fs/                   # Filesystem drivers (exFAT, LastFS, FAT, Ext2, ISO9660)
├── net/                  # TCP/IP stack, WireGuard tunnel, TCP window manager
├── crypto/               # BearSSL TLS library, BLAKE2s, Noise_IK
├── tests/                # 17 Host Unit Test Suites with 100% Vector Verification
└── Makefile              # Modular build system and disk image packager
```

---

## 🛠️ Building & Testing

### Prerequisites
On Linux (Ubuntu / Debian / Fedora / Arch):
```bash
# Ubuntu / Debian
sudo apt update && sudo apt install -y build-essential gcc nasm python3 rustc qemu-system-x86 ovmf

# Fedora
sudo dnf install -y gcc make nasm python3 rust qemu-system-x86 edk2-ovmf

# Arch Linux
sudo pacman -S base-devel gcc nasm python rust qemu-desktop edk2-ovmf
```

### Build Commands

1. **Compile Kernel ELF & Bootable Disk Image**:
   ```bash
   make image
   ```
   *Generates `build/kernel.elf` and the bootable disk image `build/lastkernel.img` containing the native Pulse Loader.*

2. **Execute Full 17-Suite Test Matrix**:
   ```bash
   make test
   ```
   *Compiles and runs all 17 self-verifying test suites with zero external runtime dependencies.*

3. **Run in QEMU (UEFI or BIOS)**:
   ```bash
   make qemu
   ```

---

## 🧪 Comprehensive Test Matrix

LastKernel includes **17 automated test suites** covering all critical subsystems:

| Test Suite | Subsystem Tested | Key Vectors & Assertions Verified |
| :--- | :--- | :--- |
| **`fde_aes_kat`** | Full Disk Encryption | FIPS-197 AES-256 KAT, XTS sector roundtrip, PBKDF2 verifiers |
| **`lastfs_tests`** | Native Journaled FS | WAL recovery, Extent allocation, CRC32C metadata integrity |
| **`wg_noise_tests`** | WireGuard / BearSSL | RFC 7693 BLAKE2s, RFC 7748 X25519, Noise_IKpsk2 handshake |
| **`fat_tests`** | FAT16 / FAT32 FS | Cluster allocation, directory traversal, file shrink & grow |
| **`iso_tests`** | ISO9660 CD-ROM FS | Directory record parsing, Rock Ridge extensions |
| **`tcp_window_tests`** | TCP Sliding Window | Dynamic window scaling, Out-of-order reassembly, RTO backoff |
| **`apk_core_tests`** | APK Transaction Engine | ACID journal rollback, dirty sector isolation |
| **`apk_deep_tests`** | APK Guard & Isolation | Process sandboxing, cgroups limit enforcement |
| **`apk_nextgen_tests`**| eBPF & WASM JIT | Instruction translation, bytecode safety verification |
| **`apk_advanced_tests`**| PE Loader & Bridge | 64-bit PE section mapping, import relocation table |
| **`hda_tests`** | Intel HD Audio | CORB/RIRB command packing, DMA stream configuration |
| **`usb_mass_tests`** | USB Mass Storage | SCSI CBW/CSW protocol, BOT transfer state machine |
| **`wifi_tests`** | Wi-Fi 802.11 Stack | WPA2/WPA3 4-way handshake, CCMP AES decryption |
| **`cmdline_tests`** | Kernel Command Line | Boot argument parser, console redirection flags |
| **`exfat_tests`** | Microsoft exFAT FS | 16-bit upcase hash, directory set checksum, 64-bit file streams |
| **`ahci_tests`** | SATA-3 AHCI HBA | 16-byte PRDTs, 48-bit LBA DMA EXT FIS serialization, ATA IDENTIFY |
| **`headphone_tests`** | Headphone DSP Engine | 10-band Harman EQ, Bauer crossfeed, 3D Woodworth ITD, 85 dBA clamp |
| **`microphone_tests`** | Microphone DSP Engine| Tri-factor VAD, spectral noise suppression, 80Hz rumble cut, RIFF WAV |
| **`gpu_3d_tests`** | 3D GPU Acceleration | 16.16 Barycentric rasterizer, Early-Z buffer, 8-light Blinn-Phong |
| **`gpu_hw_drivers_tests`**| Hardware GPU Drivers | Intel i915, AMD Radeon, NVIDIA Nouveau TDR watchdog & MSI-X |
| **`io_uring_tests`** | Asynchronous I/O | Lock-free SQ/CQ rings, pre-registered buffers, vector batch submit |
| **`bluetooth_tests`** | Bluetooth 5.3 Stack | Intel AX210 TLV patch, Realtek RTL8852 ROM, BLE scan, A2DP SBC audio |
| **`laptop_platform_tests`**| Laptop & Power Mgmt | ACPI EC 0x62/0x66, SBS 1.1 battery math, ThinkPad/ASUS SMC, CPU DVFS |

---

## 📜 License

LastKernel is released under the **MIT License**. Feel free to use, modify, and learn from the codebase.
