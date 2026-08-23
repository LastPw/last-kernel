#ifndef _LIMINE_H
#define _LIMINE_H

#include "stdint.h"

/* ---------------------------------------------------------------------------
 * Limine Boot Protocol v7 (Limine 12.5.x) — feature IDs, request structs and
 * the base-revision marker.  All pointer-sized fields are uint64_t so the
 * same layout works for the x86_64 kernel.
 * -------------------------------------------------------------------------*/

#define LIMINE_COMMON_MAGIC  0xc7b1dd30df4c8b88, 0x0a82e883a194f07b

/* Limine presents a subset of its protocol to the kernel: the base revision
 * the kernel was built against.  Our kernel is built against base revision 3
 * (the protocol of Limine 12.x).  Limine refuses to boot kernels that claim a
 * base revision newer than what the loaded Limine supports. */
#define LIMINE_BASE_REVISION 3

/* Limine scans the loaded image for this 3-qword magic (one qword of magic
 * would never match and the kernel would be treated as base revision 0, whose
 * request-search path expects a pointer array in .limine_reqs instead of raw
 * structs — the boot would panic with "entry outside kernel image"). */
#define LIMINE_BASE_REVISION_TAG \
    __attribute__((used, section(".limine_revision"))) \
    static volatile uint64_t _limine_base_revision[3] = \
        { 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, LIMINE_BASE_REVISION }

/* Requests live in the .limine_reqs section so Limine (which discovers
   requests by scanning that section) finds them.  Without the attribute the
   structs land in .data, outside Limine's kernel-image scan window, and the
   bootloader never fills the response pointers. */
#define LIMINE_REQUEST_SECTION \
    __attribute__((used, section(".limine_reqs")))

/* ── Feature identifiers ─────────────────────────────────────────────────── */
#define LIMINE_FRAMEBUFFER_REQUEST          \
    { LIMINE_COMMON_MAGIC,                  \
      0x9d5827dcd881dd75, 0xa3148604f6fab11b }

#define LIMINE_MEMMAP_REQUEST               \
    { LIMINE_COMMON_MAGIC,                  \
      0x67cf3d9d378a806f, 0xe304acdfc50c3c62 }

#define LIMINE_KERNEL_ADDRESS_REQUEST       \
    { LIMINE_COMMON_MAGIC,                  \
      0x71ba76863cc55f63, 0xb2644a48c516a487 }

#define LIMINE_HHDM_REQUEST                 \
    { LIMINE_COMMON_MAGIC,                  \
      0x48dcf1cb8ad2b852, 0x63984e959a98244b }

#define LIMINE_EXECUTABLE_CMDLINE_REQUEST   \
    { LIMINE_COMMON_MAGIC,                  \
      0x4b161536e598651e, 0xb390ad4a2f1f303a }

#define LIMINE_BOOTLOADER_INFO_REQUEST      \
    { LIMINE_COMMON_MAGIC,                  \
      0xf55038d8e2a1202f, 0x279426fcf5f59740 }

#define LIMINE_RSDP_REQUEST                 \
    { LIMINE_COMMON_MAGIC,                  \
      0xc5e77b6b397e7b43, 0x27637845accdcf3c }

/* ── Memory map entry types ──────────────────────────────────────────────── */
#define LIMINE_MEMMAP_USABLE                 0
#define LIMINE_MEMMAP_RESERVED               1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE       2
#define LIMINE_MEMMAP_ACPI_NVS               3
#define LIMINE_MEMMAP_BAD_MEMORY             4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_KERNEL_AND_MODULES     6
#define LIMINE_MEMMAP_FRAMEBUFFER            7

/* ── Response / entry structures ───────────────────────────────────────────
 * Layout matches Limine 12.5.x (base revision 3): every response struct
 * starts with a `revision` field that the bootloader sets.  Missing it
 * shifts every later field by 8 bytes. */

struct limine_video_mode {
    uint64_t pitch;
    uint64_t width;
    uint64_t height;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t unused[7];
    uint64_t mode_index;
};

struct limine_framebuffer {
    uint64_t address;    /* physical address */
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    uint64_t edid;
    /* Response revision 1 */
    uint64_t mode_count;
    uint64_t modes;
};

struct limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    struct limine_framebuffer *framebuffers;
};

struct limine_framebuffer_request {
    uint64_t id[4];                      /* LIMINE_FRAMEBUFFER_REQUEST */
    uint64_t revision;
    struct limine_framebuffer_response *response;   /* populated by Limine */
};

/* ----------------------------------------------------------------------- */

struct limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry *entries;
};

struct limine_memmap_request {
    uint64_t id[4];                      /* LIMINE_MEMMAP_REQUEST */
    uint64_t revision;
    struct limine_memmap_response *response;
};

/* ----------------------------------------------------------------------- */

struct limine_kernel_address_response {
    uint64_t revision;
    uint64_t physical_base;
    uint64_t virtual_base;
};

struct limine_kernel_address_request {
    uint64_t id[4];                      /* LIMINE_KERNEL_ADDRESS_REQUEST */
    uint64_t revision;
    struct limine_kernel_address_response *response;
};

/* ----------------------------------------------------------------------- */

/* The HHDM offset converts physical addresses to the direct-mapped higher
 * half:  phys_to_virt(p) = p + offset.  Limine maps all of physical RAM
 * there before jumping to the kernel. */
struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct limine_hhdm_request {
    uint64_t id[4];                      /* LIMINE_HHDM_REQUEST */
    uint64_t revision;
    struct limine_hhdm_response *response;
};

/* ----------------------------------------------------------------------- */

struct limine_executable_cmdline_response {
    uint64_t revision;
    const char *cmdline;
};

struct limine_executable_cmdline_request {
    uint64_t id[4];                      /* LIMINE_EXECUTABLE_CMDLINE_REQUEST */
    uint64_t revision;
    struct limine_executable_cmdline_response *response;
};

/* ----------------------------------------------------------------------- */

struct limine_bootloader_info_response {
    uint64_t revision;
    const char *name;
    const char *version;
};

struct limine_bootloader_info_request {
    uint64_t id[4];                      /* LIMINE_BOOTLOADER_INFO_REQUEST */
    uint64_t revision;
    struct limine_bootloader_info_response *response;
};

/* ----------------------------------------------------------------------- */

/* Physical address of the ACPI RSDP, found by the bootloader (which can
   consult UEFI's EFI_CONFIGURATION_TABLE, unavailable to the kernel). */
struct limine_rsdp_response {
    uint64_t revision;
    uint64_t address;
};

struct limine_rsdp_request {
    uint64_t id[4];                      /* LIMINE_RSDP_REQUEST */
    uint64_t revision;
    struct limine_rsdp_response *response;
};

#endif /* _LIMINE_H */