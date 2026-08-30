/* Pulse Loader BIOS stage1: classic 512-byte MBR boot sector.
   Loaded by BIOS at 0000:7C00. Reads stage2 (LBA 1..) to 0000:8000
   via INT 13h extensions, passes boot drive in DL, far-jumps.
   Linked with -Ttext 0x7C00; raw-binary output. */

    .code16
    .section .text

start:
    ljmp    $0x0000, $set_segs
set_segs:
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7000, %sp
    movb    %dl, %al
    movb    %al, boot_drive         /* local copy */
    movb    %dl, 0x4FE8             /* hand-off slot for stage2 */

    movw    $msg_ok, %si
    call    puts

    movb    boot_drive, %dl         /* restore drive for INT 13h */
    movw    $dap, %si
    movb    $0x42, %ah              /* extended read */
    int     $0x13
    jc      die

    movw    $msg_go, %si
    call    puts
    movw    $msg_j, %si
    call    puts
    ljmp    $0x0000, $0x8000

die:
    pushw   %ax
    movw    $msg_err, %si
    call    puts
    popw    %ax
    /* print AH error code as two hex digits */
    movb    %ah, %bl
    shrb    $4, %bl
    call    hexnib
    movb    %ah, %bl
    andb    $0xF, %bl
    call    hexnib
    movw    $crlf_s,  %si
    call    puts
1:  hlt
    jmp     1b

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
    je      2f
    movb    %al, %cl
    movw    $0x3FD, %dx
3:  inb     %dx, %al
    testb   $0x20, %al
    jz      3b
    movb    %cl, %al
    movw    $0x3F8, %dx
    outb    %al, %dx
    jmp     puts
2:  ret

msg_ok:  .asciz "stage1\r\n"
msg_go:  .asciz " -> jump\r\n"
msg_j:   .asciz "J\r\n"
msg_err: .asciz "stage1 disk err AH="
crlf_s:  .asciz "\r\n"

boot_drive:
    .byte 0x80

/* Disk Address Packet for INT 13h AH=42h */
dap:
    .byte  0x10, 0                  /* size, reserved */
    .word STAGE2_SECTORS            /* sectors to read */
    .word 0x0000, 0x0800            /* buffer 0000:8000 */
    .quad 1                         /* starting LBA */

    . = 510                         /* pad to boot signature */
    .word 0xAA55                    /* boot signature */
