/* Multiboot2 handoff from UEFI x64: tear down long mode, land in
   32-bit protected mode with EAX=0x36D76289, EBX=&MBI.
   Called as ms_abi: rcx = kernel entry, rdx = MBI physical address.
   Stage letters are written to COM1 so a silent hang shows exactly
   how far the teardown got. */
    .section .text

.macro mark chr
    pushw   %dx
    pushw   %ax
    movw    $0x3FD, %dx
99\@: inb   %dx, %al
    testb   $0x20, %al
    jz      99\@b
    movb    \chr, %al
    movw    $0x3F8, %dx
    outb    %al, %dx
    popw    %ax
    popw    %dx
.endm

    .align 8
gdt64_32:
    .quad 0x0000000000000000        /* null */
    .quad 0x00CF9A000000FFFF        /* 0x08: code32, base 0, limit 4G */
    .quad 0x00CF92000000FFFF        /* 0x10: data32 */
gdt_desc:
    .word gdt_desc - gdt64_32 - 1
    .quad gdt64_32

    .globl pulse_jump_mb2
pulse_jump_mb2:
    cli
    movq    %rdx, %rbx             /* MBI -> rbx (survives as ebx) */
    pushq   %rcx                   /* kernel entry, stashed for 32-bit */
    mark    'A'                     /* entered */

    /* Runtime-fix the GDTR base: the assembler bakes ELF VMAs, but the
       firmware loads us at ImageBase 0x400000. RIP-relative patching is
       the only safe way to hand LGDT a correct pointer. */
    leaq    gdt64_32(%rip), %rax
    leaq    gdt_desc(%rip), %rdx
    movq    %rax, 2(%rdx)
    lgdt    (%rdx)
    mark    'B'                     /* GDT loaded */

    /* far-return into a 32-bit code segment */
    lea     1f(%rip), %rax
    pushq   $0x08
    pushq   %rax
    lretq

1:  .code32
    mark    'C'                     /* executing in 32-bit CS */
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    /* leave long mode: PG off, then LME off */
    movl    %cr0, %eax
    btcl    $31, %eax               /* CR0.PG */
    movl    %eax, %cr0
    mark    'D'                     /* paging off */

    movl    $0xC0000080, %ecx       /* MSR_EFER */
    rdmsr
    btcl    $8, %eax                /* EFER.LME */
    wrmsr
    mark    'E'                     /* LME off */

    popl    %edi                    /* kernel entry */
    movl    $0x36D76289, %eax       /* MB2 magic */
    mark    'F'                     /* jumping to kernel */
    jmp     *%edi
