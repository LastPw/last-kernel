#include "bootinfo.h"
#include "multiboot2.h"
#include "gfx.h"
#include "serial.h"
#include "string.h"

/* ── Global boot geometry consumed by the kernel ─────────────────────────── */

u64 g_hhdm_offset          = 0xFFFF800000000000ULL;
u64 g_kernel_phys_base     = 0x200000ULL;
u64 g_kernel_virt_base     = 0x200000ULL;
u64 g_memmap_total_low     = 0ULL;
u64 g_boot_framebuffer_phys  = 0ULL;
u64 g_boot_framebuffer_w     = 1024ULL;
u64 g_boot_framebuffer_h     = 768ULL;
u64 g_boot_framebuffer_pitch = 1024ULL * 4ULL;
u64 g_boot_framebuffer_bpp   = 32ULL;

static char g_cmdline_buf[256];
static u64  g_rsdp_address = 0ULL;

boot_mem_region_t g_boot_regions[BOOTINFO_MAX_REGIONS];
u32 g_boot_region_count = 0u;

const char* bootinfo_cmdline(void) {
    return g_cmdline_buf;
}

u64 bootinfo_rsdp(void) {
    return g_rsdp_address;
}

u8 bootinfo_parse_boot_mode(const char* cmdline) {
    if (!cmdline || !*cmdline) return BOOT_MODE_DEFAULT;
    if (strstr(cmdline, "BOOT_MODE=fast") || strstr(cmdline, "fast")) return BOOT_MODE_FAST;
    if (strstr(cmdline, "BOOT_MODE=safe") || strstr(cmdline, "safe")) return BOOT_MODE_SAFE;
    if (strstr(cmdline, "BOOT_MODE=desktop") || strstr(cmdline, "desktop")) return BOOT_MODE_DESKTOP;
    if (strstr(cmdline, "BOOT_MODE=server") || strstr(cmdline, "server")) return BOOT_MODE_SERVER;
    return BOOT_MODE_DEFAULT;
}

int bootinfo_validate(void) {
    return (g_boot_region_count > 0u) ? 0 : -1;
}

/* ── Multiboot2 Information Parser ────────────────────────────────────────── */

void bootinfo_init(void* mbi_addr) {
    serial_init();
    serial_writeln("[bootinfo] Initializing from Pulse Loader Multiboot2 MBI...");
    memset(g_cmdline_buf, 0, sizeof(g_cmdline_buf));
    g_boot_region_count = 0u;
    g_memmap_total_low = 0u;

    if (!mbi_addr) {
        serial_writeln("[bootinfo] Warning: Null MBI pointer from bootloader.");
        g_boot_regions[0].base = 0x1000000ULL;
        g_boot_regions[0].len  = 128ULL * 1024ULL * 1024ULL;
        g_boot_region_count = 1u;
        g_memmap_total_low = g_boot_regions[0].len;
        return;
    }

    u32 total_size = *(const u32*)mbi_addr;
    const u8* ptr = (const u8*)mbi_addr + 8u; /* Skip total_size and reserved */
    const u8* end = (const u8*)mbi_addr + total_size;

    while (ptr < end) {
        const multiboot_tag_t* tag = (const multiboot_tag_t*)ptr;
        if (tag->type == MULTIBOOT_TAG_TYPE_END) {
            break;
        }

        switch (tag->type) {
            case MULTIBOOT_TAG_TYPE_CMDLINE: {
                const multiboot_tag_string_t* cmd_tag = (const multiboot_tag_string_t*)tag;
                str_ncpy(g_cmdline_buf, cmd_tag->string, sizeof(g_cmdline_buf) - 1u);
                serial_write("[bootinfo] Cmdline: ");
                serial_writeln(g_cmdline_buf);
                break;
            }

            case MULTIBOOT_TAG_TYPE_MMAP: {
                const multiboot_tag_mmap_t* mmap_tag = (const multiboot_tag_mmap_t*)tag;
                u32 num_entries = (mmap_tag->size - sizeof(multiboot_tag_mmap_t) + sizeof(multiboot_mmap_entry_t)) / mmap_tag->entry_size;
                const u8* cur_entry = (const u8*)mmap_tag->entries;

                for (u32 i = 0u; i < num_entries; i++) {
                    const multiboot_mmap_entry_t* e = (const multiboot_mmap_entry_t*)cur_entry;
                    if (e->type == MULTIBOOT_MEMORY_AVAILABLE) {
                        if (g_boot_region_count < BOOTINFO_MAX_REGIONS) {
                            extern u8 __heap_end[];
                            u64 kernel_end_pa = (u64)(uintptr_t)__heap_end;
                            if (kernel_end_pa < 0x2000000ULL) kernel_end_pa = 0x2000000ULL; /* at least 32 MiB */
                            kernel_end_pa = (kernel_end_pa + 0x1FFFFFULL) & ~0x1FFFFFULL;

                            u64 base = e->base_addr;
                            u64 len  = e->length;
                            if (base < kernel_end_pa) {
                                if (base + len > kernel_end_pa) {
                                    len -= (kernel_end_pa - base);
                                    base = kernel_end_pa;
                                } else {
                                    len = 0;
                                }
                            }
                            if (len > 0) {
                                boot_mem_region_t* r = &g_boot_regions[g_boot_region_count++];
                                r->base = base;
                                r->len  = len;
                                g_memmap_total_low += len;
                            }
                        }
                    }
                    cur_entry += mmap_tag->entry_size;
                }
                break;
            }

            case MULTIBOOT_TAG_TYPE_FRAMEBUFFER: {
                const multiboot_tag_framebuffer_t* fb_tag = (const multiboot_tag_framebuffer_t*)tag;
                g_boot_framebuffer_phys  = fb_tag->framebuffer_addr;
                g_boot_framebuffer_w     = fb_tag->framebuffer_width;
                g_boot_framebuffer_h     = fb_tag->framebuffer_height;
                g_boot_framebuffer_pitch = fb_tag->framebuffer_pitch;
                g_boot_framebuffer_bpp   = fb_tag->framebuffer_bpp;
                serial_writeln("[bootinfo] Framebuffer detected via Multiboot2 GOP.");
                break;
            }

            case MULTIBOOT_TAG_TYPE_ACPI_OLD:
            case MULTIBOOT_TAG_TYPE_ACPI_NEW: {
                const multiboot_tag_acpi_t* acpi_tag = (const multiboot_tag_acpi_t*)tag;
                g_rsdp_address = (u64)(uintptr_t)acpi_tag->rsdp;
                serial_writeln("[bootinfo] ACPI RSDP pointer discovered.");
                break;
            }

            default:
                break;
        }

        /* Tags are 8-byte aligned */
        u32 tag_size = tag->size;
        tag_size = (tag_size + 7u) & ~7u;
        if (tag_size == 0u) break;
        ptr += tag_size;
    }

    if (g_boot_region_count == 0u) {
        g_boot_regions[0].base = 0x1000000ULL;
        g_boot_regions[0].len  = 128ULL * 1024ULL * 1024ULL;
        g_boot_region_count = 1u;
        g_memmap_total_low = g_boot_regions[0].len;
    }

    serial_writeln("[bootinfo] Pulse Loader Multiboot2 MBI parsed successfully.");
}
