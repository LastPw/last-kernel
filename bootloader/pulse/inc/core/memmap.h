/* Normalized physical memory map, firmware-agnostic.
   Both the UEFI path (GetMemoryMap descriptors) and the BIOS path
   (E820 entries) convert into this model before handoff. */
#ifndef PULSE_MEMMAP_H
#define PULSE_MEMMAP_H

#include "pulsebase.h"

typedef enum {
    PULSE_MEM_USABLE = 0,      /* free RAM */
    PULSE_MEM_RESERVED,        /* firmware/hardware, do not use */
    PULSE_MEM_ACPI_RECLAIM,
    PULSE_MEM_ACPI_NVS,
    PULSE_MEM_MMIO,
    PULSE_MEM_BAD,
} pulse_mem_type_t;

#define PULSE_MEMMAP_MAX 128
#define PULSE_UEFI_PAGE  4096ULL

/* Numeric UEFI memory types (UEFI spec §7.2, memory allocation services).
   Kept here so the core does not depend on firmware headers. */
enum {
    PULSE_EFI_RESERVED = 0,
    PULSE_EFI_LOADER_CODE,
    PULSE_EFI_LOADER_DATA,
    PULSE_EFI_BS_CODE,
    PULSE_EFI_BS_DATA,
    PULSE_EFI_RT_CODE,
    PULSE_EFI_RT_DATA,
    PULSE_EFI_CONVENTIONAL,
    PULSE_EFI_UNUSABLE,
    PULSE_EFI_ACPI_RECLAIM,
    PULSE_EFI_ACPI_NVS,
    PULSE_EFI_MMIO,
    PULSE_EFI_MMIO_PORT,
    PULSE_EFI_PAL_CODE,
    PULSE_EFI_PERSISTENT,
};

typedef struct {
    UINT64          base;
    UINT64          len;
    pulse_mem_type_t type;
} pulse_mem_entry_t;

typedef struct {
    pulse_mem_entry_t entry[PULSE_MEMMAP_MAX];
    UINTN             count;
} pulse_memmap_t;

/* Convert a raw UEFI GetMemoryMap buffer into the normalized model.
   buf/bufsz describe an array of descriptors, each dsize bytes apart
   (descriptor layout per UEFI spec: Type u32, Pad u32, PhysicalStart u64,
   VirtualStart u64, NumberOfPages u64, Attribute u64). Entries that would
   overflow the table are dropped; returns number dropped. */
UINTN memmap_from_uefi(pulse_memmap_t *out, const UINT8 *buf, UINTN bufsz,
                       UINTN dsize);

/* Sort by base ascending, merge adjacent entries of equal type. */
void memmap_finalize(pulse_memmap_t *mm);

/* Convert raw E820 entries (base u64 | len u64 | type u32 [,ext u32])
   gathered by real-mode code. entry_size is stride between records
   (20 or 24). Returns dropped-entry count. */
UINTN memmap_from_e820(pulse_memmap_t *out, const UINT8 *buf,
                       UINTN count, UINTN entry_size);

/* Numeric E820 types (spec values). */
enum {
    PULSE_E820_USABLE = 1,
    PULSE_E820_RESERVED = 2,
    PULSE_E820_ACPI_RECLAIM = 3,
    PULSE_E820_ACPI_NVS = 4,
    PULSE_E820_BAD = 5,
};

const char *memmap_type_name(pulse_mem_type_t t);

#endif
