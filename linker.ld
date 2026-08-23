ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5); /* R + X */
    data PT_LOAD FLAGS(6); /* R + W */
}

SECTIONS {
    /* Kernel virtual base in the higher half (-mcmodel=kernel).  The classic
       -2GiB anchor (0xffffffff80000000) is in the kernel model's ±2GiB
       range and stays clear of Limine's default HHDM offset (0xffff800000000000).
       Limine ignores p_paddr and picks its own physical base; the
       kernel_address + hhdm responses carry the load geometry.  The whole
       memsz of every PT_LOAD (including .bss and the .heap reserve below)
       is physically backed by Limine and zeroed before _start runs. */
    . = 0xFFFFFFFF80000000;

    .text : {
        *(.text .text.*)
    } :text

    /* Limine protocol requests must be kept so the bootloader can discover
       them (some toolchains garbage-collect unused sections). */
    .limine_reqs : {
        KEEP(*(.limine_reqs))
    } :text

    /* Marked section for the base-revision tag emitted by LIMINE_BASE_REVISION_TAG. */
    .limine_revision : {
        KEEP(*(.limine_revision))
    } :text

    .rodata : {
        *(.rodata .rodata.*)
        *(.gnu.linkonce.r.*)
    } :text

    /* Each PT_LOAD must start on a fresh page: if two loads with different
       permission flags share a page, Limine refuses to load the image
       ("PHDRs with different permissions sharing the same memory page"). */
    . = ALIGN(4096);
    .data : {
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
    } :data

    __kernel_end = .;

    /* Embed the Limine UEFI loader for the P5 installer's EFI system
       partition.  Must be placed before the .bss reserve or the location
       counter would wrap back below the kernel base. */
    . = ALIGN(4096);
    .limine_efi_blob : {
        *(.limine_efi_blob)
    } :data

    . = ALIGN(4096);
    __bss_start = .;
    .bss : {
        *(COMMON)
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
    } :data
    __bss_end = .;

    /* Kernel heap backing.  Lives in the zeroed, Limine-backed image tail so
       no frames have to be carved out of the frame allocator for it.
       HW-max: 1 GiB static reserve — the RAM-disk fallback alone needs
       512 MiB, and a 4 GiB machine has plenty of address space for it. */
    . = ALIGN(4096);
    __heap_start = .;
    .heap (NOLOAD) : {
        . = . + 64 * 1024 * 1024;
    } :data
    __heap_end = .;

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note*)
    }
}
