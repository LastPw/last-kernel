/*
 * bootinfo.h — resolved Limine boot contract for the x86_64 kernel.
 *
 * The stage1 checksum contract is gone: Limine hands the kernel everything
 * (HHDM offset, physical/virtual bases, memmap, framebuffer, cmdline) through
 * the request pointers this header exposes.  kmain() consumes it once and
 * freezes the derived values into the globals below.
 */

#ifndef BOOTINFO_H
#define BOOTINFO_H

#include "types.h"
#include "limine.h"

/* Legacy flag namespace kept for kernel code that tests boot_flags. */
enum {
    BOOTINFO_FLAG_SAFE_FALLBACK = 0x0020u,
    BOOTINFO_FLAG_KERNEL_CHECKSUM_BAD = 0x0010u,
    BOOTINFO_MAGIC = 0x4C425446u /* "LBFT" — local marker, not written by Limine */
};

/* Boot-mode ids parsed from the limine cmdline (BOOT_MODE=...).  Values are
   the legacy bootinfo enum so existing kernel code keeps its meaning. */
enum {
    BOOT_MODE_DEFAULT  = 0u,
    BOOT_MODE_FAST     = 1u,
    BOOT_MODE_SAFE     = 2u,
    BOOT_MODE_DESKTOP  = 3u,
    BOOT_MODE_SERVER   = 4u,
    BOOT_MODE_VPS      = 5u
};

/* Derive the boot-mode id from the limine cmdline string. */
u8 bootinfo_parse_boot_mode(const char* cmdline);

/* The limine cmdline string, or 0 if the request was not answered. */
const char* bootinfo_cmdline(void);

/* Physical address of the ACPI RSDP as reported by the bootloader (0 if
   unavailable).  In long mode this is the only reliable source under UEFI:
   the legacy EBDA scan window is not part of any memmap entry, so it is not
   mapped through the HHDM. */
u64 bootinfo_rsdp(void);

/* Globals populated by kmain()/core_init() from the limine responses. */
extern u64 g_hhdm_offset;          /* phys_to_virt(p) = p + g_hhdm_offset */
extern u64 g_kernel_phys_base;     /* physical base of the kernel image    */
extern u64 g_kernel_virt_base;     /* virtual  base of the kernel image    */
extern u64 g_memmap_total_low;     /* total usable RAM, bytes              */
extern u64 g_boot_framebuffer_phys;
extern u64 g_boot_framebuffer_w;
extern u64 g_boot_framebuffer_h;
extern u64 g_boot_framebuffer_pitch;
extern u64 g_boot_framebuffer_bpp;

/* Validate that the limine requests were answered (kernel was booted by a
   limine-compatible loader).  Returns 0 on success. */
int bootinfo_validate(void);

/* Temporary debug dump of the raw limine response pointers over serial. */
void bootinfo_debug_dump(void);

#endif /* BOOTINFO_H */