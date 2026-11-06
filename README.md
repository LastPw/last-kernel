# LastKernel

A modular, monolithic 64-bit operating system kernel developed for the `x86_64` architecture, written in C11, Assembly, and Rust.

[![Architecture](https://img.shields.io/badge/arch-x86__64%20SMP-blue.svg?style=flat-square)]()
[![Language](https://img.shields.io/badge/language-C11%20%7C%20NASM%20%7C%20Rust-informational.svg?style=flat-square)]()
[![Bootloader](https://img.shields.io/badge/bootloader-Pulse%20Loader-orange.svg?style=flat-square)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg?style=flat-square)]()
[![Author](https://img.shields.io/badge/author-Taha%20Vaziry-purple.svg?style=flat-square)]()

---

## Overview

**LastKernel** is a freestanding 64-bit monolithic operating system kernel designed for modern x86_64 hardware. It operates in higher-half long mode, featuring a native bootloader, multi-core SMP scheduling, comprehensive memory management, an extensible virtual filesystem, hardware device drivers, and a POSIX / Linux binary compatibility layer.

---

## Architecture & Subsystems

### Core & Architecture (`arch/x86_64`, `kernel/`)
* **Long Mode Direct Map:** Boots into pure 64-bit higher-half space (`0xFFFF800000000000`) with 4-level paging (PML4).
* **Multi-Core SMP:** Multiprocessor initialization using APIC / IOAPIC with an AP trampoline.
* **Preemptive Scheduling:** Fair quantum task scheduler supporting kernel threads, user tasks, signals, and process isolation.
* **Compatibility Layer:** Linuxulator ABI layer supporting x86_64 ELF binary execution and Linux syscall emulation.

### Memory Management (`mm/`)
* **Physical Frame Allocator:** Bitmap and zone-based frame allocation with NUMA awareness.
* **Virtual Memory:** Dynamic paging, demand paging, page fault handling, and separate user address spaces.
* **Kernel Heap & Slabs:** Low-fragmentation slab caches and dynamic kernel allocator.

### Storage & Filesystems (`fs/`, `storage/`)
* **Virtual File System (VFS):** Unified mount point hierarchy and file descriptor table.
* **Filesystem Drivers:**
  * **LastFS 2.0:** Native journaling filesystem with extent allocation and CRC32C integrity.
  * **FAT16 / FAT32 & exFAT:** Full read/write support and cluster management.
  * **ISO9660:** CD-ROM filesystem with Rock Ridge extensions.
* **Storage Drivers:** NVMe PCIe controller, SATA AHCI, and IDE block device support.

### Networking Stack (`net/`)
* **Protocol Suite:** Ethernet II, ARP, IPv4, IPv6, ICMP, UDP, and a sliding-window TCP implementation.
* **Network Services:** Embedded DHCP client/server and DNS resolver.
* **Network Interfaces:** Drivers for Intel E1000/E1000e, Realtek RTL8139/RTL8169, and VirtIO-Net.

### Drivers & Hardware Support (`drivers/`)
* **Display & Video:** Framebuffer console, DRI/DRM KMS interface, and software rasterization pipeline.
* **Audio:** Intel HD Audio (HDA) and AC'97 controller drivers.
* **Input & Peripherals:** PS/2 keyboard/mouse, serial UART (COM1-COM4), and USB (xHCI/OHCI) support.
* **Power Management:** ACPI table parsing, power state transitions, and HPET / RTC timing.

---

## Repository Structure

```text
last-kernel/
├── arch/x86_64/      # Architecture-specific bootstrap, paging, and CPU setup
├── bootloader/pulse/ # Native Pulse bootloader (UEFI PE32+ & legacy BIOS)
├── crypto/           # Embedded cryptographic routines and TLS engine
├── drivers/          # Device drivers (video, audio, storage, input, network)
├── fs/               # Filesystem implementations (LastFS, FAT, exFAT, ISO9660)
├── include/          # Subsystem and kernel header files
├── kernel/           # Core kernel, scheduler, syscalls, and Linux compatibility
├── mm/               # Physical, virtual, and heap memory management
├── net/              # TCP/IP network stack and protocol engines
├── rust_kernel/      # Rust safety modules and verification extensions
├── storage/          # Storage management and network storage drivers
├── tests/            # Automated kernel and subsystem unit test suites
└── tools/            # Build utilities and installer components
```

---

## Building & Running

### Prerequisites
On standard Linux distributions (Debian, Ubuntu, Fedora, Arch):
```bash
# Required packages: GCC cross-compiler or native GCC, NASM, Make, Python 3, QEMU
sudo apt update && sudo apt install -y build-essential gcc nasm python3 qemu-system-x86 ovmf
```

### Build Targets

* **Build Kernel & Bootable Image:**
  ```bash
  make image
  ```
  Produces `build/kernel.elf` and the bootable disk image `build/lastkernel.img`.

* **Run Unit Tests:**
  ```bash
  make test
  ```
  Compiles and executes the automated subsystem test matrix.

* **Run in QEMU:**
  ```bash
  make qemu
  ```

---

## License

This project is licensed under the **MIT License**.

**Author:** Taha Vaziry
