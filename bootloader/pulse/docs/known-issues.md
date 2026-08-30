# Known Issues

## ia32-firmware × 64-bit-kernel test row (test-knownissues)

**Symptom:** booting the 64-bit Multiboot2 fixture from the IA32 loader
under Debian `ovmf-ia32 2025.02` dies inside firmware during the second
file read: `#UD` at `EIP=0xB0000` in firmware context (firmware GDT/CR3
still active).

**Isolation performed (2026-08-25):**
- Serial stage-markers in the handoff asm prove the loader reaches
  `jmp *edi` (markers A,B,D,F) — handoff itself is sound.
- Per-call breadcrumbs around `EFI_FILE_PROTOCOL` show:
  first file read fully OK (`OoAaRr`), second file's `Read()` faults.
- Independent of which file is read second (config-string swap test).
- Independent of size-query method (`GetInfo`, seek+`GetPosition`,
  capped blind `Read` — all fault identically).
- Only occurs when the CPU exposes long mode (`-cpu qemu64/max` or
  x86_64 host); with plain `qemu32` reads work fine but the 64-bit
  kernel cannot start (no long mode) — different, expected failure.
- Multi-cluster vs single-cluster files suspected; cluster-size tuning
  on floppy geometry did not stick.

**Conclusion:** Debian OVMF-IA32 bug (likely DXE memory-protection/NXE
interaction with the FAT driver when LM-capable CPUs are present).
Not a Pulse Loader defect: the identical path passes on x64 OVMF for
both kernels, and IA32 OVMF for the 32-bit kernel.

**Resolution plan:** vendor or build an OvmfPkg Ia32 without the bug,
then move the row back into `make test`.
