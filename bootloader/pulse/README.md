# Pulse Loader

A from-scratch Multiboot2 bootloader for x86, booting on UEFI (IA-32 and
x86-64) and legacy BIOS from one shared core. Loads ELF32/ELF64 kernels,
hands off in 32-bit protected mode per the Multiboot2 spec.

```
Pulse Loader/
├── Makefile              all targets; `make test` runs the boot matrix
├── inc/                  headers, grouped by subsystem
│   ├── core/             firmware-agnostic: pulsebase, lib, elf,
│   │                     memmap (+e820), mb2 builder, config, vfs contract
│   └── uefi/             uefi.h (spec structs + layout asserts), fs helpers
├── src/
│   ├── core/             portable loader logic (host-unit-tested)
│   ├── uefi/             UEFI entry flow, file I/O, PE linker scripts
│   ├── bios/             stage1 MBR, stage2 (E820+PM switch), flat core
│   └── arch/
│       ├── i386/         serial.S, jump_mb2.S (paging-off handoff)
│       └── x86_64/       serial.S, jump_mb2.S (long-mode teardown)
├── testkern/             Multiboot2 fixture kernels (k32, k64) — acceptance tests
├── tests/                host-side unit tests (`make test-core`)
├── tools/                fix_pe.py, mkbios.py, run-test.sh, vendored OVMF-IA32
└── docs/                 filesystems.md (10 FS models), known-issues.md
```

## Build & test

```sh
make              # EFI binaries (IA32+X64), FAT images, BIOS images
make test         # unit tests + full boot matrix (QEMU, headless)
make size         # footprint report
make run          # interactive boot under OVMF
```

Boot matrix rows live in `Makefile` and drive QEMU via
`tools/run-test.sh`, asserting kernel banners over COM1:

| row | firmware | loader | payload |
|-----|----------|--------|---------|
| x64-k32 / x64-k64 | OVMF X64 | X64 | k32 / k64 |
| ia32-k32          | OVMF IA32 | IA32 | k32 |
| bios-k32 / bios-k64 | SeaBIOS | BIOS chain | k32 / k64 |

Known firmware bug affecting one cell: see `docs/known-issues.md`.

## Design rules

- read-only bootloader; no dynamic allocation beyond mount arenas
- every baked address must equal its runtime address (link at final VA;
  `fix_pe.py` converts VMA→RVA so PE files stay compact)
- struct layouts pinned by `_Static_assert`; offsets verified against
  real firmware
- everything testable on the host first, then QEMU, then real hardware
