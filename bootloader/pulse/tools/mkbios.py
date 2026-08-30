#!/usr/bin/env python3
"""Assemble the BIOS boot image:
   LBA0      stage1 (512 B MBR)
   LBA1-16   stage2 (8 KiB)
   LBA17-48  loader core (flat binary, 16 KiB)
   LBA49..   kernel A, then kernel B (sector-aligned)
usage: mkbios.py <out.img> <stage1> <stage2> <core> <kernA> <kernB>
"""
import sys

S2_LBA, S2_SECT = 1, 16
CORE_LBA, CORE_SECT = 17, 32
KERN_LBA = 49
SECT = 512

def pad(b, size):
    assert len(b) <= size, f"overflow: {len(b)} > {size}"
    return b + b"\x00" * (size - len(b))

out, s1, s2, core, ka, kb = sys.argv[1:7]
ka_bytes = open(ka, "rb").read()
kb_bytes = open(kb, "rb").read()
sect_a = (len(ka_bytes) + SECT - 1) // SECT

img = bytearray()
img += pad(open(s1, "rb").read(), 512)
img += pad(open(s2, "rb").read(), S2_SECT * SECT)
img += pad(open(core, "rb").read(), CORE_SECT * SECT)
assert img and len(img) == KERN_LBA * SECT
img += pad(ka_bytes, sect_a * SECT)
lba_b = KERN_LBA + sect_a
img += kb_bytes
open(out, "wb").write(img)
print(f"mkbios: {out}: {len(img)} bytes | A@LBA{KERN_LBA} "
      f"({len(ka_bytes)} B), B@LBA{lba_b} ({len(kb_bytes)} B)")
assert lba_b == 49 + sect_a
