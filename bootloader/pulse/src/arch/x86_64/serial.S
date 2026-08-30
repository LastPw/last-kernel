/* COM1 output, x86-64 SysV ABI: rdi = char ptr / char.
   Hand-rolled to keep the loader's footprint minimal. */
    .section .text

    .globl pulse_serial_put
pulse_serial_put:
    pushq   %rbx
    movl    %edi, %ebx              /* save char */
    movw    $0x3FD, %dx             /* COM1+5: Line Status Register */
1:  inb     %dx, %al
    testb   $0x20, %al              /* transmit-hold empty? */
    jz      1b
    movl    %ebx, %eax
    movw    $0x3F8, %dx
    outb    %al, %dx
    popq    %rbx
    ret

    .globl pulse_serial_puts
pulse_serial_puts:                  /* rdi = NUL-terminated string */
    pushq   %rbx
    movq    %rdi, %rsi
1:  lodsb
    testb   %al, %al
    je      2f
    movb    %al, %bl
    movw    $0x3FD, %dx
3:  inb     %dx, %al
    testb   $0x20, %al
    jz      3b
    movb    %bl, %al
    movw    $0x3F8, %dx
    outb    %al, %dx
    jmp     1b
2:  popq    %rbx
    ret

    .globl pulse_serial_hex
pulse_serial_hex:                   /* rdi = value: 16 hex chars, no newline */
    pushq   %rbx
    pushq   %rcx
    movq    %rdi, %rbx
    movl    $16, %ecx
1:  rolq    $4, %rbx
    /* poll transmit-hold empty BEFORE touching the value */
    movw    $0x3FD, %dx             /* LSR */
2:  inb     %dx, %al
    testb   $0x20, %al
    jz      2b
    movl    %ebx, %eax
    andl    $0xF, %eax
    cmpb    $10, %al
    jb      3f
    addb    $('A'-10), %al
    jmp     4f
3:  addb    $'0', %al
4:  movw    $0x3F8, %dx             /* data port */
    outb    %al, %dx
    decl    %ecx
    jnz     1b
    popq    %rcx
    popq    %rbx
    ret
