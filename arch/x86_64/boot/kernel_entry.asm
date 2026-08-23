[bits 64]

section .text

extern kmain
extern __bss_start
extern __bss_end
extern __heap_start

global _start
_start:
    ; Limine enters the kernel in long mode with paging enabled: the kernel
    ; image is mapped at its VMA, all physical RAM at the HHDM offset, and
    ; the lower 4 GiB identity map is NOT present (base revision >= 1).
    ; Every GPR is zeroed by Limine before entry; no register carries the
    ; bootloader info — kmain reads the limine request responses instead.
    cli
    cld
    mov al, 'E'
    out 0xE9, al

    ; Set up our own kernel stack in the high half (Limine's stack is not
    ; to be relied upon past entry).  The boot stack lives just below the
    ; heap reserve so it never collides with .bss.
    lea rsp, [rel boot_stack_top]
    and rsp, -16

    ; Zero BSS so all statics start from a known state.
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor rax, rax
    shr rcx, 3
    rep stosq

    mov al, 'B'
    out 0xE9, al

    call kmain
.hang:
    cli
    hlt
    jmp .hang

; Resume continuation for a killed/retired user task: iret lands here with a
; fresh kernel stack (TSS.rsp0 is set to boot_stack_top by gdt_init), so the
; shell's frames from before the user detour are gone and the C stack below
; this frame is untouched garbage-free.  shell_resume() drops the stale idle
; frame and runs the command loop again.
global kernel_resume_trampoline
extern shell_resume
kernel_resume_trampoline:
    cli
    lea rsp, [rel boot_stack_top]
    and rsp, -16
    cld
    call shell_resume
.hang2:
    cli
    hlt
    jmp .hang2

section .bss
align 16
global boot_stack_top
global boot_stack_bottom
boot_stack_bottom:
    ; 512 KiB: boot bring-up (disk/fs/persist) nests deep before the first
    ; real task runs, and IRQ frames land on this same stack.  16 KiB was
    ; blown through mid-disk-init and the following IRQ frame spilled past
    ; the region into .bss.
    resb 524288
boot_stack_top:
