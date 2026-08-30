#!/usr/bin/env python3
"""Post-process a PE32/PE32+ produced by objcopy from our ELF.

objcopy packs section raw data tightly after 0x200 bytes of headers, but UEFI
firmware is happiest with EDK2-style images: FileAlignment == SectionAlignment
== 0x1000, SizeOfHeaders == 0x1000, and each section's file offset equal to
its RVA. fix_pe.py rewrites the file into that shape:

  - rebuilds the file with headers zero-padded to 0x1000 and each section's
    data placed exactly at its RVA (idempotent)
  - sets FileAlignment / SizeOfHeaders accordingly
  - clears IMAGE_FILE_RELOCS_STRIPPED (objcopy claims it wrongly)
  - fixes ImageBase=0 (page 0 is unmapped) and Win32VersionValue garbage
  - recomputes SizeOfImage from the real section extent
"""
import struct
import sys

RELOCS_STRIPPED = 0x1


def main(path):
    with open(path, "rb") as f:
        old = bytearray(f.read())

    pe = struct.unpack_from("<I", old, 0x3C)[0]
    if bytes(old[pe:pe + 4]) != b"PE\x00\x00":
        sys.exit(f"{path}: not a PE file")

    coff = pe + 4
    nsec = struct.unpack_from("<H", old, coff + 2)[0]
    optsz = struct.unpack_from("<H", old, coff + 16)[0]
    oh = coff + 20
    magic = struct.unpack_from("<H", old, oh)[0]
    secalign = struct.unpack_from("<I", old, oh + 32)[0]
    sec_off = oh + optsz

    changed = []

    # --- read section table + blobs from wherever they currently live ---
    sections = []  # [off, vaddr, vsize, blob]
    for i in range(nsec):
        off = sec_off + i * 40
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", old, off + 8)
        blob = bytes(old[rawptr:rawptr + rawsize]) if rawsize else None
        sections.append([off, vaddr, vsize, blob])

    # --- patch header fields in `old` first ---
    chars = struct.unpack_from("<H", old, coff + 18)[0]
    if chars & RELOCS_STRIPPED:
        struct.pack_into("<H", old, coff + 18, chars & ~RELOCS_STRIPPED)
        changed.append("cleared RELOCS_STRIPPED")

    IMAGE_BASE = 0x400000
    base_off = oh + (24 if magic == 0x20B else 28)
    fmt = "<Q" if magic == 0x20B else "<I"
    if struct.unpack_from(fmt, old, base_off)[0] != IMAGE_BASE:
        struct.pack_into(fmt, old, base_off, IMAGE_BASE)
        changed.append("ImageBase -> 0x400000")

    # The ELF links at runtime VAs (ImageBase+RVA); the PE needs RVAs.
    # Detect already-converted files (section VAs below the base) so the
    # tool stays idempotent.

    if struct.unpack_from("<I", old, oh + 64)[0] != 0:
        struct.pack_into("<I", old, oh + 64, 0)
        changed.append("zeroed Win32VersionValue")

    converted = all(va < IMAGE_BASE for _o, va, _vs, _b in sections if va)
    if not converted:
        for sec in sections:
            sec[1] = sec[1] - IMAGE_BASE          # vaddr -> rva
            struct.pack_into("<I", old, sec[0] + 12, sec[1])
        ep_off = oh + 16
        ep = struct.unpack_from("<I", old, ep_off)[0]
        if ep >= IMAGE_BASE:
            struct.pack_into("<I", old, ep_off, ep - IMAGE_BASE)
        changed.append("sections VMA->RVA")

    end = max((vaddr + vsize for _o, vaddr, vsize, _b in sections if vsize), default=0)
    new_size = (end + secalign - 1) // secalign * secalign
    struct.pack_into("<I", old, oh + 56, new_size)

    header_size = secalign  # EDK2 convention
    header_end = pe + 4 + 20 + optsz + nsec * 40

    moved = any(rawptr != vaddr for _o, _va, _vs, _b in
                [(o, va, vs, struct.unpack_from('<I', old, o + 20)[0])
                 for o, va, vs, b in sections])

    if moved or struct.unpack_from("<I", old, oh + 60)[0] != header_size:
        for off, vaddr, _vsize, _blob in sections:
            struct.pack_into("<I", old, off + 20, vaddr)   # PointerToRawData=RVA
        struct.pack_into("<I", old, oh + 36, header_size)  # FileAlignment
        struct.pack_into("<I", old, oh + 60, header_size)  # SizeOfHeaders
        changed.append(f"data placed at RVA offsets (align {header_size:#x})")
        need_rebuild = True
    else:
        need_rebuild = False

    if struct.unpack_from("<I", old, oh + 56)[0] != new_size:
        changed.append(f"SizeOfImage -> {new_size:#x}")

    # --- build output: patched headers, zero pad, blobs at RVAs ---
    out = bytearray(old[:header_end])            # headers with all patches
    out += b"\x00" * (header_size - len(out))    # pad to SizeOfHeaders
    for off, vaddr, _vsize, blob in sections:
        if not blob:
            continue
        out += b"\x00" * (vaddr - len(out))
        out += blob                              # data lands exactly at RVA

    with open(path, "wb") as f:
        f.write(out)
    print(f"fix_pe: {path}: " + ("; ".join(changed) if changed else "already clean"))


if __name__ == "__main__":
    main(sys.argv[1])
