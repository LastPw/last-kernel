/* COM1 output, IA-32 cdecl: args on the stack.
   Hand-rolled to keep the loader's footprint minimal. */
    .section .text

    .globl pulse_serial_put
pulse_serial_put:                   /* 4(%esp) = char */
    movl    4(%esp), %eax
    movb    %al, %cl
    pushl   %edx
    movw    $0x3FD, %dx             /* COM1+5: Line Status Register */
1:  inb     %dx, %al
    testb   $0x20, %al              /* transmit-hold empty? */
    jz      1b
    movw    $0x3F8, %dx
    movb    %cl, %al
    outb    %al, %dx
    popl    %edx
    ret

    .globl pulse_serial_puts
pulse_serial_puts:                  /* 12(%esp) after two pushes */
    pushl   %esi
    pushl   %ebx
    movl    12(%esp), %esi
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
2:  popl    %ebx
    popl    %esi
    ret

    .globl pulse_serial_hex
pulse_serial_hex:                   /* [esp+4] = value: 8 hex chars */
    pushl   %ebx
    pushl   %ecx
    pushl   %edx
    movl    16(%esp), %ebx
    movl    $8, %ecx
1:  roll    $4, %ebx
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
    popl    %edx
    popl    %ecx
    popl    %ebx
    ret
