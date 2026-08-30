/* Minimal 64-bit Multiboot2 kernel: entered in 32-bit protected mode
   (paging off) per the MB2 spec, climbs to long mode, banners, halts. */
    .section .mb2hdr,"a"
    .balign 4
mbh:
    .long 0xE85250D6
    .long 0
    .long mbh_end - mbh
    .long -(0xE85250D6 + 0 + (mbh_end - mbh))
    .short 0                        /* end tag */
    .short 0
    .long 8
mbh_end:

    .section .text
    .code32
    .globl _start
_start:
    cli

    /* identity-map first 1 GiB: PML4[0]->PDPT[0]->PD[512 x 2MiB] */
    movl    $pdpt, %eax
    orl     $3, %eax                /* present | writable */
    movl    $pml4, %edi
    movl    %eax, (%edi)
    movl    $pd, %eax
    orl     $3, %eax
    movl    %eax, (%edi)

    movl    $pd, %edi
    xorl    %ecx, %ecx
1:  movl    %ecx, %eax
    shll    $21, %eax               /* i * 2MiB */
    orl     $0x83, %eax             /* PS | RW | P */
    movl    %eax, (%edi)
    addl    $8, %edi                /* high dword stays zero (<4G) */
    incl    %ecx
    cmpl    $512, %ecx
    jb      1b

    movl    $pml4, %eax
    movl    %eax, %cr3
    movl    %cr4, %eax
    orl     $0x20, %eax             /* CR4.PAE */
    movl    %eax, %cr4
    movl    $0xC0000080, %ecx       /* MSR_EFER */
    rdmsr
    orl     $0x100, %eax            /* EFER.LME */
    wrmsr
    lgdt    gdt_desc

    movl    %cr0, %eax
    orl     $0x80000001, %eax       /* PG | PE -> long mode active */
    movl    %eax, %cr0

    ljmp    $0x08, $lm_entry        /* execute in 64-bit code segment */

    .code64
lm_entry:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss

    leaq    msg(%rip), %rsi
2:  lodsb
    testb   %al, %al
    je      3f
    movb    %al, %cl
    movw    $0x3FD, %dx             /* LSR */
4:  inb     %dx, %al
    testb   $0x20, %al
    jz      4b
    movb    %cl, %al
    movw    $0x3F8, %dx
    outb    %al, %dx
    jmp     2b

3:  cli
6:  hlt
    jmp     6b

    .align 8
gdt:
    .quad 0x0000000000000000        /* null */
    .quad 0x00AF9A000000FFFF        /* 0x08: code64 (L=1) */
    .quad 0x00CF92000000FFFF        /* 0x10: data */
gdt_desc:
    .word gdt_desc - gdt - 1
    .quad gdt

    .section .rodata
msg:
    .ascii "\r\n[PULSE-K64-LONGMODE]\r\n\0"

    .section .bss
    .balign 4096
pml4:   .space 4096
pdpt:   .space 4096
pd:     .space 4096
