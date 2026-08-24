#include "bootinfo.h"
#include "gfx.h"
#include "serial.h"
#include "string.h"

/* ── Debug helper: dump a u64 as hex on the serial port ───────────────────── */
static void dbg_hex(u64 v) {
    static const char hexd[] = "0123456789abcdef";
    for (int i = 15; i >= 0; i--) {
        serial_putc(hexd[(v >> (i * 4)) & 0xf]);
    }
    serial_putc('\n');
}

/* ── Limine protocol requests ──────────────────────────────────────────────
 * These structs are discovered by Limine through the .limine_reqs section
 * (see linker.ld) and their `response` pointers are filled before _start.
 * Every request must be volatile so the compiler keeps the magic alive and
 * re-reads the response each time. */

LIMINE_REQUEST_SECTION
static volatile struct limine_framebuffer_request limine_fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};
LIMINE_REQUEST_SECTION
static volatile struct limine_memmap_request limine_memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};
LIMINE_REQUEST_SECTION
static volatile struct limine_kernel_address_request limine_kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};
LIMINE_REQUEST_SECTION
static volatile struct limine_hhdm_request limine_hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};
LIMINE_REQUEST_SECTION
static volatile struct limine_executable_cmdline_request limine_cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST,
    .revision = 0
};
LIMINE_REQUEST_SECTION
static volatile struct limine_bootloader_info_request limine_bootloader_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST,
    .revision = 0
};
LIMINE_REQUEST_SECTION
static volatile struct limine_rsdp_request limine_rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0
};

LIMINE_BASE_REVISION_TAG;

void bootinfo_debug_dump(void) {
    serial_writeln("[bootinfo] dbg:");
    serial_writeln("  fb.response:");
    dbg_hex((u64)limine_fb_request.response);
    if (limine_fb_request.response != 0) {
        const volatile u64* fbraw = (const volatile u64*)limine_fb_request.response;
        serial_writeln("  fb.raw[0..5]:");
        for (int i = 0; i < 6; i++) dbg_hex(fbraw[i]);
        serial_writeln("  fb.count:");
        dbg_hex(limine_fb_request.response->framebuffer_count);
        struct limine_framebuffer** fblist =
            (struct limine_framebuffer**)limine_fb_request.response->framebuffers;
        serial_writeln("  fb.fb0:");
        dbg_hex((u64)fblist[0]);
        if (limine_fb_request.response->framebuffer_count > 0 && fblist[0] != 0) {
            volatile struct limine_framebuffer* fb = fblist[0];
            serial_writeln("  fb0.addr/w/h/pitch:");
            dbg_hex(fb->address); dbg_hex(fb->width); dbg_hex(fb->height); dbg_hex(fb->pitch);
        }
    }
    serial_writeln("  memmap.response:");
    dbg_hex((u64)limine_memmap_request.response);
    if (limine_memmap_request.response != 0) {
        serial_writeln("  memmap.count:");
        dbg_hex(limine_memmap_request.response->entry_count);
    }
    serial_writeln("  hhdm.response:");
    dbg_hex((u64)limine_hhdm_request.response);
    if (limine_hhdm_request.response != 0) {
        serial_writeln("  hhdm.offset:");
        dbg_hex(limine_hhdm_request.response->offset);
    }
    serial_writeln("  kaddr.response:");
    dbg_hex((u64)limine_kernel_address_request.response);
}

/* ── Frozen boot contract consumed by the rest of the kernel ─────────────── */

u64 g_hhdm_offset = 0;

/* Usable-RAM region table (post-Limine), consumed by the frame allocator.
   Regions are physical [base, base+len) spans the allocator may hand out. */
#define BOOT_MAX_REGIONS 16u /* must match bootinfo.h */
boot_mem_region_t g_boot_regions[BOOT_MAX_REGIONS];
u32 g_boot_region_count = 0;
u64 g_kernel_phys_base = 0;
u64 g_kernel_virt_base = 0;
u64 g_memmap_total_low = 0;
u64 g_boot_framebuffer_phys = 0;
u64 g_boot_framebuffer_w = 0;
u64 g_boot_framebuffer_h = 0;
u64 g_boot_framebuffer_pitch = 0;
u64 g_boot_framebuffer_bpp = 0;

int bootinfo_validate(void) {
    if (limine_memmap_request.response == 0 ||
        limine_hhdm_request.response == 0 ||
        limine_kernel_address_request.response == 0) {
        serial_writeln("[bootinfo] missing limine responses");
        return -1;
    }
    g_hhdm_offset = limine_hhdm_request.response->offset;
    g_kernel_phys_base = limine_kernel_address_request.response->physical_base;
    g_kernel_virt_base = limine_kernel_address_request.response->virtual_base;

    g_memmap_total_low = 0;
    struct limine_memmap_response* mm = limine_memmap_request.response;
    struct limine_memmap_entry** entries = (struct limine_memmap_entry**)mm->entries;
    for (u64 i = 0; i < mm->entry_count; i++) {
        u64 base = entries[i]->base, len = entries[i]->length;
        /* This Limine fork reindexes memmap types 0-based:
           0=usable, 6=kernel-and-modules (the bulk RAM span).
           Both are pool-eligible once the kernel image itself is
           excluded by the frame allocator. */
        int pool_eligible = (entries[i]->type == LIMINE_MEMMAP_USABLE ||
                             entries[i]->type == LIMINE_MEMMAP_KERNEL_AND_MODULES);
        if (pool_eligible) {
            g_memmap_total_low += len;
        }
        if (pool_eligible &&
            g_boot_region_count < BOOT_MAX_REGIONS && len >= FRAME_SIZE_4K) {
            g_boot_regions[g_boot_region_count].base = base;
            g_boot_regions[g_boot_region_count].len = len;
            g_boot_region_count++;
        }
        if (base <= 0x20000000ull && base + len >= 0x1fb7e000ull) {
            serial_write("[bootinfo] has 0x1fb7e000: type=");
            serial_write_dec((u32)entries[i]->type);
            serial_write(" base=0x");
            serial_write_hex((u32)base);
            serial_write(" len=0x");
            serial_write_hex((u32)len);
            serial_writeln("");
        }
    }

#ifdef BOOT_MEMMAP_VERBOSE
    for (u64 i = 0; i < mm->entry_count; i++) {
        serial_write("[memmap] n=");
        serial_write_dec(mm->entry_count);
        serial_write(" i=");
        serial_write_dec((u32)i);
        serial_write(" t=");
        serial_write_dec((u32)entries[i]->type);
        serial_write(" base_mib=");
        serial_write_dec((u32)(entries[i]->base >> 20));
        serial_write(" len_mib=");
        serial_write_dec((u32)(entries[i]->length >> 20));
        serial_putc('\n');
    }
#endif

    if (limine_fb_request.response != 0 && limine_fb_request.response->framebuffer_count > 0) {
        struct limine_framebuffer** fblist =
            (struct limine_framebuffer**)limine_fb_request.response->framebuffers;
        struct limine_framebuffer* fb = fblist[0];
        g_boot_framebuffer_phys = fb->address;
        g_boot_framebuffer_w = fb->width;
        g_boot_framebuffer_h = fb->height;
        g_boot_framebuffer_pitch = fb->pitch;
        g_boot_framebuffer_bpp = fb->bpp;

        /* Limine framebuffer address: if already virtual/higher-half, keep as-is; otherwise add HHDM offset. */
        u64 virt_addr = fb->address;
        if (virt_addr < 0xFFFF800000000000ull && virt_addr < g_hhdm_offset) {
            virt_addr += g_hhdm_offset;
        }

        serial_write("[bootinfo] fb: addr=0x");
        serial_write_hex((u32)(fb->address >> 32));
        serial_write_hex((u32)fb->address);
        serial_write(" virt=0x");
        serial_write_hex((u32)(virt_addr >> 32));
        serial_write_hex((u32)virt_addr);
        serial_write(" ");
        serial_write_dec((u32)fb->width);
        serial_write("x");
        serial_write_dec((u32)fb->height);
        serial_write("@");
        serial_write_dec((u32)fb->bpp);
        serial_writeln("");

        gfx_set_boot_framebuffer(fb->address, virt_addr,
                                 (u32)fb->width, (u32)fb->height,
                                 (u32)fb->pitch, fb->bpp);
    }
    return 0;
}

u64 bootinfo_rsdp(void) {
    return (limine_rsdp_request.response != 0) ? limine_rsdp_request.response->address : 0u;
}

const char* bootinfo_cmdline(void) {
    return (limine_cmdline_request.response != 0)
               ? limine_cmdline_request.response->cmdline
               : 0;
}

u8 bootinfo_parse_boot_mode(const char* cmdline) {
    if (!cmdline) return BOOT_MODE_DEFAULT;
    /* Accept "BOOT_MODE=desktop|server|safe|fast|vps" anywhere in the line. */
    static const char* const names[] = {
        [BOOT_MODE_DEFAULT] = "BOOT_MODE=default",
        [BOOT_MODE_FAST] = "BOOT_MODE=fast",
        [BOOT_MODE_SAFE] = "BOOT_MODE=safe",
        [BOOT_MODE_DESKTOP] = "BOOT_MODE=desktop",
        [BOOT_MODE_SERVER] = "BOOT_MODE=server",
        [BOOT_MODE_VPS] = "BOOT_MODE=vps"
    };
    for (u8 m = BOOT_MODE_DEFAULT; m <= BOOT_MODE_VPS; m++) {
        const char* n = names[m];
        const char* p = cmdline;
        while (*p) {
            if (*p == *n && str_ncmp(p, n, str_len(n)) == 0) return m;
            p++;
        }
    }
    return BOOT_MODE_DEFAULT;
}
