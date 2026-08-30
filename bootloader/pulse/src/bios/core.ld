/* Flat-binary core for the BIOS path. Entry symbol must be first in
   .text so stage2 can `call 0x10000` directly. */
OUTPUT_FORMAT("elf32-i386")
ENTRY(core_entry)

SECTIONS
{
  . = 0x10000;
  .text : {
    *(.text.core_entry)
    *(.text*)
    *(.rodata*)
  }
  .data : { *(.data*) }
  .bss  : { *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.note*) }
}
