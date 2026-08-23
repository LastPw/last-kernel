[bits 64]

extern isr_handler
extern irq_handler

global isr0
global isr1
global isr2
global isr3
global isr4
global isr5
global isr6
global isr7
global isr8
global isr9
global isr10
global isr11
global isr12
global isr13
global isr14
global isr15
global isr16
global isr17
global isr18
global isr19
global isr20
global isr21
global isr22
global isr23
global isr24
global isr25
global isr26
global isr27
global isr28
global isr29
global isr30
global isr31
global isr128

global irq0
global irq1
global irq2
global irq3
global irq4
global irq5
global irq6
global irq7
global irq8
global irq9
global irq10
global irq11
global irq12
global irq13
global irq14
global irq15

; ---------------------------------------------------------------------------
; Interrupt frame (rsp) after isr_entry/irq_entry pushes, before CPU frame:
;   [0]  rbp   [8]  r15  [16] r14  [24] r13  [32] r12  [40] r11
;   [48] r10   [56] r9   [64] r8   [72] rdi  [80] rsi  [88] rdx
;   [96] rcx  [104] rbx  [112] rax  [120] int_no  [128] err_code
; then CPU-pushed: rip, cs, rflags, rsp, ss (ring3 -> ring0 via TSS.rsp0).
; Matches struct registers_t exactly (include/idt.h).
;
; ISR_NOERR pushes a 0 placeholder for err_code; ISR_ERR keeps the CPU error
; code.  The common path restores ALL 15 pushed slots, then does
;   add rsp, 16   ; drop int_no + err_code (fixed size on both paths)
;   iretq
;
; rbp is pushed first and popped last so a rewritten frame (scheduler
; selected a different task) restores the target task's frame pointer.
; iretq itself only restores rip/cs/rflags/rsp/ss.
; ---------------------------------------------------------------------------
%macro ISR_NOERR 1
isr%1:
    push qword 0
    push qword %1
    jmp isr_entry
%endmacro

%macro ISR_ERR 1
isr%1:
    push qword %1
    jmp isr_entry
%endmacro

%macro IRQ 2
irq%1:
    push qword 0
    push qword %2
    jmp irq_entry
%endmacro

; entry states: rsp -> [int_no][err_code] then CPU frame
isr_entry:
    push rbp
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    mov rdi, rsp
    call isr_handler
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    pop rbp
    add rsp, 16
    iretq

irq_entry:
    push rbp
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    mov rdi, rsp
    call irq_handler
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    pop rbp
    add rsp, 16
    iretq

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31
ISR_NOERR 128

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47
