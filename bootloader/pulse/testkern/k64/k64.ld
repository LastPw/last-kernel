OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

SECTIONS
{
  . = 0x200000;
  .text : { KEEP(*(.mb2hdr)) *(.text*) }
  .rodata : { *(.rodata*) }
  .bss : { *(.bss*) }
  /DISCARD/ : { *(.comment) *(.note*) }
}
