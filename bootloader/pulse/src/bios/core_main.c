/* BIOS-path loader core: runs in 32-bit protected mode (paging off,
   identity-mapped) at 0x10000. stage2 has already placed both kernel
   images in memory; this core builds the memory map, shows the VGA
   boot menu and hands the chosen kernel to the Multiboot2 entry —
   all shared code with the UEFI path.
   (FAT-backed loading is staged: driver + tests are in place, the
   PM<->RM disk shim for stage2 is next.) */
#include "bios_ui.h"
#include "elf.h"
#include "memmap.h"
#include "mb2.h"
#include "lib.h"

extern void pulse_serial_puts(const char *);
extern void pulse_jump_mb2(UINTN entry, UINTN mbi);   /* cdecl on ia32 */

#ifndef PULSE_DEFAULT_PICK
#define PULSE_DEFAULT_PICK 0
#endif

#define MBI_ADDR   0x22000u
#define MBI_CAP    (8 * 1024u)

static VOID *bios_alloc(VOID *ctx, UINT64 phys, UINT64 len)
{
    /* identity environment: physical == linear; trust the memory map */
    return (VOID *)(UINTN)phys;
}

__attribute__((section(".text.core_entry")))
void core_entry(UINT32 e820_count, UINT32 e820_buf,
                UINT32 len_a, UINT32 buf_a,
                UINT32 len_b, UINT32 buf_b)
{
    pulse_serial_puts("\r\nPulse Loader v0.4 (BIOS)\r\n");

    static const char *const titles[] = {
        "kernel A", "kernel B",
    };
    UINT32 lens[2] = { len_a, len_b };
    UINT32 bufs[2] = { buf_a, buf_b };

    UINTN pick = bios_menu(2, titles, 5, PULSE_DEFAULT_PICK < 2
                                              ? PULSE_DEFAULT_PICK : 0);
    if (lens[pick] == 0)
        pick ^= 1;

    pulse_memmap_t mm;
    memmap_from_e820(&mm, (const UINT8 *)(UINTN)e820_buf,
                     e820_count, 24);

    const elf_callbacks_t cb = { bios_alloc };
    UINT64 entry = 0, top = 0;
    if (elf_load((const UINT8 *)(UINTN)bufs[pick], lens[pick],
                 &cb, NULL, &entry, &top) != ELF_OK) {
        pulse_serial_puts("kernel load failed\r\n");
        goto hang;
    }
    pulse_serial_puts("loaded\r\n");

    mb2_builder_t mb;
    mb2_init(&mb, (VOID *)(UINTN)MBI_ADDR, MBI_CAP);
    mb2_tag_string(&mb, 1, "pulse-bios");
    mb2_tag_string(&mb, 2, "PulseLoader");
    mb2_tag_memmap(&mb, &mm);
    if (!mb2_finish(&mb)) {
        pulse_serial_puts("MBI overflow\r\n");
        goto hang;
    }
    pulse_serial_puts("jump\r\n");
    pulse_jump_mb2(entry, MBI_ADDR);

hang:
    for (;;)
        __asm__ volatile ("hlt");
}
