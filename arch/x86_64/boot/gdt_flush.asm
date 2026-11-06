[bits 64]

global gdt_flush
global ltr_tss

section .text

; gdt_flush(u64 gdt_ptr_addr): lgdt + reload segment registers + far ret.
gdt_flush:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ; Far return to reload CS with the 64-bit kernel code selector.
    push 0x08
    lea rax, [rel .flush]
    push rax
    retfq
.flush:
    ret

; ltr_tss(): load the TSS selector (0x28 = slots 5+6).
ltr_tss:
    mov ax, 0x28
    ltr ax
    ret