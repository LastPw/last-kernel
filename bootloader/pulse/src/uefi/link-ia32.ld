/* ia32 linker script — EDK2-style: sections at 0x1000, file offsets = RVAs */
OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(efi_main)

SECTIONS
{
  /* Link at TRUE RUNTIME ADDRESSES: firmware honors ImageBase 0x400000
     (set in tools/fix_pe.py), so VMAs must live at ImageBase+RVA. This
     makes every baked address (i386 GOT slots, asm data pointers)
     correct without relocation. fix_pe.py converts VMA->RVA for the
     PE headers so the file itself stays compact. */
  . = 0x401000;
  .text 0x401000 : ALIGN(0x1000) { *(.text*) *(.rodata*) }
  .data        : ALIGN(0x1000) { *(.sdata*) *(.data*) *(.eh_frame)
                                 *(.sbss*) *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.note*) }
}
