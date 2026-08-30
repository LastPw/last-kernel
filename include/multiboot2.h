#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

#include "types.h"

#define MULTIBOOT2_HEADER_MAGIC             0xE85250D6u
#define MULTIBOOT2_BOOTLOADER_MAGIC         0x36D76289u

#define MULTIBOOT_TAG_TYPE_END              0u
#define MULTIBOOT_TAG_TYPE_CMDLINE          1u
#define MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME 2u
#define MULTIBOOT_TAG_TYPE_MODULE           3u
#define MULTIBOOT_TAG_TYPE_BASIC_MEMINFO    4u
#define MULTIBOOT_TAG_TYPE_BOOTDEV          5u
#define MULTIBOOT_TAG_TYPE_MMAP             6u
#define MULTIBOOT_TAG_TYPE_VBE              7u
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER      8u
#define MULTIBOOT_TAG_TYPE_ELF_SECTIONS     9u
#define MULTIBOOT_TAG_TYPE_APM              10u
#define MULTIBOOT_TAG_TYPE_EFI32            11u
#define MULTIBOOT_TAG_TYPE_EFI64            12u
#define MULTIBOOT_TAG_TYPE_SMBIOS           13u
#define MULTIBOOT_TAG_TYPE_ACPI_OLD         14u
#define MULTIBOOT_TAG_TYPE_ACPI_NEW         15u
#define MULTIBOOT_TAG_TYPE_NETWORK          16u
#define MULTIBOOT_TAG_TYPE_EFI_MMAP         17u
#define MULTIBOOT_TAG_TYPE_EFI_BS           18u
#define MULTIBOOT_TAG_TYPE_EFI32_IH         19u
#define MULTIBOOT_TAG_TYPE_EFI64_IH         20u
#define MULTIBOOT_TAG_TYPE_LOAD_BASE_ADDR   21u

#define MULTIBOOT_MEMORY_AVAILABLE          1u
#define MULTIBOOT_MEMORY_RESERVED           2u
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE   3u
#define MULTIBOOT_MEMORY_NVS                4u
#define MULTIBOOT_MEMORY_BADRAM             5u

typedef struct {
    u32 type;
    u32 size;
} __attribute__((packed)) multiboot_tag_t;

typedef struct {
    u32 type;
    u32 size;
    char string[1];
} __attribute__((packed)) multiboot_tag_string_t;

typedef struct {
    u64 base_addr;
    u64 length;
    u32 type;
    u32 reserved;
} __attribute__((packed)) multiboot_mmap_entry_t;

typedef struct {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
    multiboot_mmap_entry_t entries[1];
} __attribute__((packed)) multiboot_tag_mmap_t;

typedef struct {
    u32 type;
    u32 size;
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8  framebuffer_bpp;
    u8  framebuffer_type;
    u16 reserved;
} __attribute__((packed)) multiboot_tag_framebuffer_t;

typedef struct {
    u32 type;
    u32 size;
    u8 rsdp[1];
} __attribute__((packed)) multiboot_tag_acpi_t;

#endif /* MULTIBOOT2_H */
