# Pulse Loader — Filesystem Support Models

Status: design baseline. Ten read-only filesystem models, ordered by when
they land. Every driver plugs into one tiny VFS layer (`inc/vfs.h`) and
obeys one memory discipline. This is the contract; implementation comes
per milestone.

## Ground rules (apply to all ten)

| Rule | Value |
|------|-------|
| Access | read-only. Writers are out of scope forever. |
| Allocation | mount-time arena from `AllocatePool`, capped **64 KiB/volume** |
| Sector I/O | one bounce buffer **4 KiB** per volume, reused |
| Metadata cache | **8 entries × 1 KiB**, direct-mapped, no eviction logic fancier than round-robin |
| Files | streamed through caller buffer; **never** buffered whole |
| Code budget | ≤ 6 KB text/driver after `-Os` + gc-sections (`make size` gates it) |
| Recursion | forbidden. Iterative walks only, explicit depth caps |
| Failure mode | unknown feature flags → clean `UNSUPPORTED`, never guess |

Detection is always signature-probe based; we never "try drivers until
one mounts". Partition discovery (MBR types, GPT GUIDs) lives below this
layer and maps partition type → candidate driver list.

---

## 1. FAT12 — floppies, legacy images

- **Probe:** BPB at LBA 0, `bytes/sec ∈ {512,1024}`, cluster count < 4085.
- **Layout:** reserved sectors, N FATs (12-bit packed entries, `idx*1.5`
  odd/even byte dance), fixed root-dir region (32 B entries), data area.
- **Read path:** BPB → linear root scan → cluster chain walk → LBA =
  `data_start + (clus-2)*spc`. Depth cap 65 519 clusters.
- **Risk:** trivial. **~400 LOC. Lands with M4 (BIOS path).**

## 2. FAT16 — small HD / USB

- Same machine as FAT12 with 16-bit entries; cluster count 4085–65524.
- Shares ≥ 80 % of code with FAT12 — implemented as one module,
  `fatcore.c`, three thin frontends.
- **~350 LOC net. M4.**

## 3. FAT32 — EFI system partition companion

- 28-bit entries (`& 0x0FFF FFFF`), EOC `≥ 0x0FFF FFF8`; root dir is a
  cluster chain pointed by BPB32.
- **LFN:** 0x0F-attr entries carrying UTF-16 fragments, order via
  sequence numbers + name checksum over the 8.3 entry. Required — real
  kernels have long names.
- The BIOS frontend must read the ESP-class partition *without firmware
  help*: this driver is what makes BIOS boot equal to UEFI boot.
- **~600 LOC with LFN. M4, blocking.**

## 4. exFAT — big removable media

- Probe: `"EXFAT   "` in boot sector.
- 32 B directory-entry *sets* (file entry → stream extension → filename
  extensions), 64-bit cluster counts, allocation bitmap, upcase table,
  optional checksummed boot region.
- Quirk worth having: `NoFatChain` flag marks fully contiguous files →
  single-extent reads, zero chain walking.
- **Note:** patent-encumbered in some jurisdictions; ship as separate
  build-time option `PULSE_EXFAT=1`.
- **~800 LOC. Tier C (post-M6).**

## 5. ISO 9660 — CDs, DVDs, rescue media

- Probe: `"CD001"` at byte offset 1 of sector 16 (2048 B sectors).
- Primary Volume Descriptor → root directory record; directory records
  are self-delimiting (`length` byte first), both-endian u32 sizes.
- El Torito catalog pointer lives beside the PVD → lets us *find and
  chainload* other images from CD later.
- Name layers, in order of preference: Rock Ridge `NM` → Joliet SVD
  (UCS-2) → bare 8.3 uppercase.
- **~500 LOC + RR/Joliet name extras. Tier A (with M4/M5 matrix).**

## 6. UDF — Blu-ray, some USB sticks

- Probe: `"\x01BEOSTYPE"`-free — actually anchor at sector 256
  (AVDP), then Partition + Logical Volume Descriptors, logical block
  size, File Set Descriptor → root ICB.
- File Entries carry allocation descriptors (`short_ad`/`long_ad`),
  contiguous vs. mapped extents flagged per descriptor; embedded data
  (file inside the ICB) supported.
- Descriptor CRCs verified; bad CRC = refuse volume.
- **~900 LOC. Tier B.**

## 7. ext2 — classic Linux

- Probe: superblock magic `0xEF53` at offset 1024.
- Group descriptors → block/inode bitmaps + inode table; inode has
  12 direct + 1/2/3-indirect block pointers — the canonical textbook
  layout, deliberately implemented first so ext4 reuses its dirent and
  group machinery.
- Variable-length dirents (`rec_len`), hardening: reject `rec_len < 8`
  and misaligned strides instead of looping forever.
- **~600 LOC. Tier B, before ext4.**

## 8. ext4 — default Linux filesystem (high priority)

- Probe: superblock + `desc_size`, feature flags. We *require*
  `INCOMPAT_EXTENTS` support and **refuse** volumes using features we
  didn't implement (inline_data, encrypt, verity, metadata_csum until
  proven).
- Extent tree in `i_block`: header + index nodes down to leaf extents
  mapping logical → physical runs; unwritten/preallocated handled as
  zeros past EOF logic.
- Journal: **not replayed.** If `s_state != clean` → refuse with a
  clear message. (Replay is a correctness tar pit; a dirty ext4 is not
  a safe boot source anyway.)
- htree directories: ignored for correctness — linear dirent scan is
  slower but exact.
- **~1200 LOC. Tier B, flagship Linux target.**

## 9. NTFS — Windows disks (read-only core)

- Probe: `"NTFS    "` in boot sector; gives MFT cluster, cluster size,
  MFT record size.
- **Fixup arrays** applied to every multi-sector structure before use —
  skipping this is the classic NTFS-driver corruption bug.
- `$MFT` walk → file records; attribute lists parsed resident +
  non-resident with run-list decoding (variable-length offset/length
  nibbles).
- v1 scope: unnamed `$DATA` streams only, `$FILE_NAME` win32 namespace,
  directory `$INDEX_ROOT`/`$INDEX_ALLOCATION` B-tree walk. Compressed,
  sparse-ok?, encrypted (EFS) → clean refusal.
- **~1500 LOC. Highest practical value / highest complexity ratio.
  Tier C.**

## 10. Btrfs — modern CoW Linux (read-only subset)

- Probe: `"_BHRfS_M"` superblock (primary at 64 KiB, mirrored).
- Mandatory even for reads: **chunk tree walk** to translate device
  offsets ↔ logical addresses; superblock points at chunk/data trees.
- B-tree item model: `DIR_ITEM`/`DIR_INDEX` for names, `INODE_ITEM`,
  `EXTENT_DATA` for file mapping (inline vs. regular extents).
- v1 restrictions enforced at mount: single device, no RAID profiles,
  **no compression** (zlib/lzo/zstd would drag in decompressors —
  zstd maybe later as an isolated module), csum verification via
  crc32c where hardware offers it.
- **~2000 LOC + strictness. Last of the ten; honest about being the
  hardest.**

---

## Out of scope (documented so nobody asks twice)

XFS (niche for boot volumes, heavy AG machinery), ZFS (a porting project
disguised as a driver), F2FS, FAT variants with vendor tweaks.

## Cost summary

| Tier | Drivers | Net new LOC (est.) | When |
|------|---------|--------------------|------|
| A | FAT12/16/32, ISO 9660 | ~1850 | M4–M5 |
| B | ext2, ext4, UDF | ~2700 | M5–M6 |
| C | exFAT, NTFS, Btrfs | ~4300 | post-M6 |

Total ≈ 8.9 KLOC of read-only FS code — acceptable because every line
is host-unit-tested (image-file fixtures + a host `blkdev` shim, same
pattern as `tests/test_core.c`) before it ever touches QEMU.

## Verification ladder (per driver)

1. Host unit tests: crafted/mkfs'd image fixtures, full lookup+read
   assertions, adversarial truncation cases.
2. QEMU matrix row: same fixture disk, real firmware, banner proof.
3. `make size`: driver stays inside its 6 KB text budget.
