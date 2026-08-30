#ifndef BOOTINFO_H
#define BOOTINFO_H

#include "types.h"

/* Legacy flag namespace kept for kernel code that tests boot_flags. */
enum {
    BOOTINFO_FLAG_SAFE_FALLBACK = 0x0020u,
    BOOTINFO_FLAG_KERNEL_CHECKSUM_BAD = 0x0010u,
    BOOTINFO_MAGIC = 0x4C425446u /* "LBFT" */
};

/* Boot-mode ids parsed from the boot cmdline (BOOT_MODE=...). */
enum {
    BOOT_MODE_DEFAULT  = 0u,
    BOOT_MODE_FAST     = 1u,
    BOOT_MODE_SAFE     = 2u,
    BOOT_MODE_DESKTOP  = 3u,
    BOOT_MODE_SERVER   = 4u,
    BOOT_MODE_VPS      = 5u
};

/* Physical usable-memory region exported by the boot loader parse. */
typedef struct {
    u64 base;
    u64 len;
} boot_mem_region_t;

#define FRAME_SIZE_4K 4096ull
#define BOOTINFO_MAX_REGIONS 64u

extern boot_mem_region_t g_boot_regions[BOOTINFO_MAX_REGIONS];
extern u32 g_boot_region_count;

/* Derive the boot-mode id from the cmdline string. */
u8 bootinfo_parse_boot_mode(const char* cmdline);

/* The cmdline string. */
const char* bootinfo_cmdline(void);

/* Physical address of the ACPI RSDP. */
u64 bootinfo_rsdp(void);

/* Globals populated from the Multiboot2 info structure. */
extern u64 g_hhdm_offset;          /* phys_to_virt(p) = p + g_hhdm_offset */
extern u64 g_kernel_phys_base;     /* physical base of the kernel image    */
extern u64 g_kernel_virt_base;     /* virtual  base of the kernel image    */
extern u64 g_memmap_total_low;     /* total usable RAM, bytes              */
extern u64 g_boot_framebuffer_phys;
extern u64 g_boot_framebuffer_w;
extern u64 g_boot_framebuffer_h;
extern u64 g_boot_framebuffer_pitch;
extern u64 g_boot_framebuffer_bpp;

/* Validate that bootinfo was populated properly. Returns 0 on success. */
int bootinfo_validate(void);

void bootinfo_init(void* mbi_addr);

#endif /* BOOTINFO_H */
