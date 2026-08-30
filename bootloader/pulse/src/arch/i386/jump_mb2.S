/* Multiboot2 handoff from UEFI IA-32: reload flat segments, drop paging,
   jump. cdecl: [esp+4] = entry, [esp+8] = MBI physical address.

   The firmware loads this PE at ImageBase 0x400000 while the ELF links
   VMAs at 0x1000-ish, so EVERY absolute address baked by the assembler
   is wrong at runtime. We derive the load delta once with the classic
   call/pop trick and never touch a baked-in absolute afterwards. */
    .section .text

.macro mark chr
    pushw   %dx
    pushw   %ax
    movw    $0x3FD, %dx
99\@: inb   %dx, %al
    testb   $0x20, %al
    jz      99\@b
    movb    $\chr, %al
    movw    $0x3F8, %dx
    outb    %al, %dx
    popw    %ax
    popw    %dx
.endm

    .align 4
gdt_mb2:
    .quad 0x0000000000000000        /* null */
    .quad 0x00CF9A000000FFFF        /* 0x08: code32 */
    .quad 0x00CF92000000FFFF        /* 0x10: data32 */
gdt_desc:
    .word gdt_desc - gdt_mb2 - 1
    .long 0                         /* base filled in at runtime */

    .globl pulse_jump_mb2
pulse_jump_mb2:
    cli
    mark    'A'
    movl    4(%esp), %edi           /* kernel entry */
    movl    8(%esp), %ebp           /* MBI */

    call    7f
7:  popl    %edx                    /* runtime address of 7 */

    /* GDTR <- {limit, runtime(gdt_mb2)} */
    leal    (gdt_desc - 7b)(%edx), %esi
    leal    (gdt_mb2  - 7b)(%edx), %eax
    movl    %eax, 2(%esi)
    lgdt    (%esi)
    mark    'B'

    /* far-jump to our code32 segment via stack pointer — no absolutes */
    xorl    %ecx, %ecx
    movw    $0x08, %cx
    pushl   %ecx                    /* CS */
    leal    (1f - 7b)(%edx), %ecx   /* runtime address of 1 */
    pushl   %ecx                    /* EIP */
    ljmp    *(%esp)

1:  movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    movl    %cr0, %eax
    andl    $0x7FFFFFFF, %eax       /* CR0.PG = 0 */
    movl    %eax, %cr0
    mark    'D'

    movl    $0x36D76289, %eax       /* MB2 magic */
    movl    %ebp, %ebx              /* MBI -> EBX for the kernel */
    mark    'F'
    jmp     *%edi
