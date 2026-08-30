OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

SECTIONS
{
  . = 0x100000;
  .text : { KEEP(*(.mb2hdr)) *(.text*) }
  .rodata : { *(.rodata*) }
  /DISCARD/ : { *(.comment) *(.note*) }
}
