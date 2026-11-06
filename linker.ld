ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5); /* R + X */
    data PT_LOAD FLAGS(6); /* R + W */
}

SECTIONS {
    . = 0x200000;

    .text : {
        KEEP(*(.mb2hdr))
        *(.text .text.*)
    } :text

    .rodata : {
        *(.rodata .rodata.*)
        *(.gnu.linkonce.r.*)
    } :text

    . = ALIGN(4096);
    .data : {
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
    } :data

    __kernel_end = .;

    . = ALIGN(4096);
    __bss_start = .;
    .bss : {
        *(COMMON)
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
    } :data
    __bss_end = .;

    . = ALIGN(4096);
    __heap_start = .;
    .heap (NOLOAD) : {
        . = . + 8 * 1024 * 1024;
    } :data
    __heap_end = .;

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note*)
        *(.limine_reqs)
        *(.limine_revision)
        *(.limine_efi_blob)
    }
}
