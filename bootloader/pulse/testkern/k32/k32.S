/* Minimal 32-bit Multiboot2 kernel: banner on COM1, halt. */
    .section .mb2hdr,"a"
    .balign 4
mbh:
    .long 0xE85250D6                /* magic */
    .long 0                         /* arch: i386 */
    .long mbh_end - mbh             /* header length */
    .long -(0xE85250D6 + 0 + (mbh_end - mbh))
    .short 0                        /* end tag */
    .short 0
    .long 8
mbh_end:

    .section .text
    .globl _start
_start:
    movl    $msg, %esi
1:  lodsb
    testb   %al, %al
    je      2f
    movb    %al, %cl
    movw    $0x3FD, %dx             /* LSR */
3:  inb     %dx, %al
    testb   $0x20, %al
    jz      3b
    movb    %cl, %al
    movw    $0x3F8, %dx
    outb    %al, %dx
    jmp     1b

2:  cli
4:  hlt
    jmp     4b

    .section .rodata
msg:
    .ascii "\r\n[PULSE-K32-BOOTED]\r\n\0"
