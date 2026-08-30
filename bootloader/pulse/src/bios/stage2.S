/* Pulse Loader BIOS stage2 — real mode at 0x8000, then protected mode.
   1. capture E820 map      -> records @0x5000 (stride 24), count @0x4FE0
   2. load flat loader core -> 0x10000
   3. load kernel ELF bytes -> kernel A @0x30000, kernel B @0x38000
   4. enable A20, enter protected mode (paging off), call core entry.
   Core ABI (cdecl): core_entry(u32 e820_count, u32 e820_buf,
                               u32 lenA, u32 bufA, u32 lenB, u32 bufB)
   Linked -Ttext 0x8000; raw-binary output.
   NOTE: everything here must stay .code16 except the explicit .code32
   protected-mode block; GAS defaults to 32-bit encodings otherwise. */

#define E820_BUF    0x5000
#define E820_COUNTA 0x4FE0
#ifndef CORE_SECTORS
#define CORE_SECTORS 32
#endif
#ifndef KERN_A_SECTORS
#define KERN_A_SECTORS 9
#endif
#ifndef KERN_B_SECTORS
#define KERN_B_SECTORS 11
#endif
#define KERN_B_LBA (49 + KERN_A_SECTORS)

    .code16
    .section .text

start:
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x6F00, %sp
    movb    0x4FE8, %al             /* boot drive from stage1 slot */
    movb    %al, bios_drive
    movw    $msg_s2, %si
    call    puts

    /* ---- 1. E820 ---- */
    xorl    %ebx, %ebx
    movl    $E820_BUF, %edi
    movw    $0, e820_cnt
1:  movl    $0x534D4150, %eax       /* 'SMAP' */
    movl    $24, %ecx
    int     $0x15
    jc      e820_done
    cmpl    $0x534D4150, %eax
    jne     e820_done
    cmpw    $20, %cx
    jae     2f
    movw    $20, %cx                /* normalize short entries */
2:  addl    $24, %edi               /* uniform stride for the parser */
    incl    e820_cnt
    testl   %ebx, %ebx
    jnz     1b
e820_done:
    movw    $msg_e820, %si
    call    puts

    /* ---- 2. loader core -> 0x10000 ---- */
    movw    $CORE_SECTORS, %cx
    movw    $0x1000, %bx            /* 0x1000:0000 = 0x10000 */
    movl    $17, dap_lba
    call    read_lba
    movw    $msg_core, %si
    call    puts

    /* ---- 3a. kernel A -> 0x30000 ---- */
    movw    $KERN_A_SECTORS, %cx
    movw    $0x3000, %bx
    movl    $49, dap_lba
    call    read_lba
    movw    $msg_kernA, %si
    call    puts

    /* ---- 3b. kernel B -> 0x38000 ---- */
    movw    $KERN_B_SECTORS, %cx
    movw    $0x3800, %bx
    movl    $KERN_B_LBA, dap_lba
    call    read_lba
    movw    $msg_kernB, %si
    call    puts

    /* ---- 4. A20 + protected mode ---- */
    inb     $0x92, %al
    orb     $2, %al                 /* fast A20 gate */
    outb    %al, $0x92

    cli
    lgdt    gdtdesc
    movl    %cr0, %eax
    orl     $1, %eax                /* CR0.PE, paging stays off */
    movl    %eax, %cr0
    ljmp    $0x08, $pm_entry

    .code32
pm_entry:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    $0x2F000, %esp

    pushl   $0x38000                /* kernB_buf */
    movw    $KERN_B_SECTORS, %ax
    shll    $9, %eax
    pushl   %eax                    /* kernB_len */
    pushl   $0x30000                /* kernA_buf */
    movw    $KERN_A_SECTORS, %ax
    shll    $9, %eax
    pushl   %eax                    /* kernA_len */
    pushl   $E820_BUF               /* e820_buf */
    movzwl  e820_cnt, %eax
    pushl   %eax                    /* e820_count */
    movl    $0x10000, %eax
    call    *%eax                   /* core_entry */

hang:
    hlt
    jmp     hang

/* ---- helpers: back to 16-bit encoding ---- */
    .code16

/* read_lba: CX = sector count, BX = target segment, dap_lba = LBA */
read_lba:
    movw    %cx, dap_count
    movw    %bx, dap_seg
    movb    bios_drive, %dl
    movb    $0x42, %ah
    movw    $dap, %si
    int     $0x13
    jnc     1f
    movw    $msg_derr, %si
    call    puts
    movb    %ah, %bl
    shrb    $4, %bl
    call    hexnib
    movb    %ah, %bl
    andb    $0xF, %bl
    call    hexnib
    movw    $crlf_s, %si
    call    puts
2:  hlt
    jmp     2b
1:  ret

hexnib:
    movb    %bl, %al
    cmpb    $10, %al
    jb      3f
    addb    $('A'-10), %al
    jmp     4f
3:  addb    $'0', %al
4:  movb    %al, %cl
    movw    $0x3FD, %dx
5:  inb     %dx, %al
    testb   $0x20, %al
    jz      5b
    movb    %cl, %al
    movw    $0x3F8, %dx
    outb    %al, %dx
    ret

puts:
    lodsb
    testb   %al, %al
    je      6f
    movb    %al, %cl
    movw    $0x3FD, %dx
7:  inb     %dx, %al
    testb   $0x20, %al
    jz      7b
    movb    %cl, %al
    movw    $0x3F8, %dx
    outb    %al, %dx
    jmp     puts
6:  ret

/* ---- data ---- */
msg_s2:    .asciz "stage2\r\n"
msg_e820:  .asciz "e820\r\n"
msg_core:  .asciz "core\r\n"
msg_kernA: .asciz "kernA\r\n"
msg_kernB: .asciz "kernB\r\n"
msg_derr:  .asciz "disk err AH="
crlf_s:    .asciz "\r\n"

e820_cnt:
    .word 0
bios_drive:
    .byte 0x80

/* Disk Address Packet */
dap:
    .byte  0x10, 0
dap_count:
    .word 0
    .word 0x0000                  /* offset */
dap_seg:
    .word 0                       /* segment */
dap_lba:
    .quad 0

.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF        /* 0x08 code32 */
    .quad 0x00CF92000000FFFF        /* 0x10 data32 */
gdtdesc:
    .word gdtdesc - gdt - 1
    .long gdt
