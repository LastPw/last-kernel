[bits 16]

global ap_trampoline_start
global ap_trampoline_end
global ap_trampoline_cr3
global ap_trampoline_stack
global ap_trampoline_entry

ap_trampoline_start:
    cli
    xor ax, ax
    mov ds, ax
    lgdt [0x8000 + ap_gdt_ptr - ap_trampoline_start]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:(0x8000 + ap_protected - ap_trampoline_start)

[bits 32]
ap_protected:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov eax, [0x8000 + ap_trampoline_cr3 - ap_trampoline_start]
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    jmp 0x08:(0x8000 + ap_paged - ap_trampoline_start)

ap_paged:
    mov esp, [0x8000 + ap_trampoline_stack - ap_trampoline_start]
    call [0x8000 + ap_trampoline_entry - ap_trampoline_start]
.halt:
    cli
    hlt
    jmp .halt

align 4
ap_gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
ap_gdt_end:
ap_gdt_ptr:
    dw ap_gdt_end - ap_gdt - 1
    dd 0x8000 + ap_gdt - ap_trampoline_start

ap_trampoline_cr3: dd 0
ap_trampoline_stack: dd 0
ap_trampoline_entry: dd 0

ap_trampoline_end:
