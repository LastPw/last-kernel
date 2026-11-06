#!/usr/bin/env python3
"""Assemble a bootable BIOS disk with a real FAT partition:
   LBA0      stage1 MBR (+ partition table entry patched in)
   LBA1-16   stage2
   LBA17-48  loader core (flat binary)
   LBA64+    FAT volume (built with mformat/mtools) containing
             /boot/pulse.conf, /boot/k32.elf, /boot/k64.elf
usage: mkdisk.py <out> <stage1> <stage2> <core> <volsize_sectors>
                 <conf> <k32> <k64>
"""
import subprocess, sys, tempfile, os

PART_LBA = 64

def main():
    out, s1, s2, core, volsec, conf, k32, k64 = sys.argv[1:9]
    vol_bytes = int(volsec) * 512

    # FAT volume via mtools (real filesystem, not synthetic)
    fd, tmp = tempfile.mkstemp(suffix=".img")
    os.close(fd)
    os.unlink(tmp)
    subprocess.run(["mformat", "-C", "-i", tmp, "-f", "2880", "::"],
                   check=True)
    subprocess.run(["mmd", "-i", tmp, "::/boot"], check=True)
    subprocess.run(["mcopy", "-i", tmp, conf, "::/boot/pulse.conf"],
                   check=True)
    subprocess.run(["mcopy", "-i", tmp, k32, "::/boot/k32.elf"], check=True)
    subprocess.run(["mcopy", "-i", tmp, k64, "::/boot/k64.elf"], check=True)
    vol = open(tmp, "rb").read()
    os.unlink(tmp)
    assert len(vol) <= vol_bytes, f"volume {len(vol)} > {vol_bytes}"
    vol += b"\x00" * (vol_bytes - len(vol))

    disk = bytearray()
    disk += pad(open(s1, "rb").read(), 512)
    disk += pad(open(s2, "rb").read(), 16 * 512)
    disk += pad(open(core, "rb").read(), 32 * 512)
    disk += b"\x00" * (PART_LBA * 512 - len(disk))   # gap to partition
    disk += vol

    # patch MBR partition entry #0: type 0x0E (FAT16B LBA), start PART_LBA
    e = 446
    disk[e:e + 16] = bytes([0x80]) + b"\xFE\xFF\xFF" + bytes([0x0E]) + \
                     b"\xFE\xFF\xFF" + \
                     PART_LBA.to_bytes(4, "little") + \
                     (len(vol) // 512).to_bytes(4, "little")

    open(out, "wb").write(disk)
    print(f"mkdisk: {out}: {len(disk)} bytes, partition @LBA{PART_LBA}")

def pad(b, size):
    assert len(b) <= size, f"overflow {len(b)} > {size}"
    return b + b"\x00" * (size - len(b))

main()
