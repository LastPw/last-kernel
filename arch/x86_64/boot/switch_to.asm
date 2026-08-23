[bits 64]
section .text

global cpu_switch_to
global kthread_entry_stub

extern gdt_set_kernel_stack
extern paging_space_switch
extern task_exit

; ============================================================================
;  cpu_switch_to(task_struct_t* prev, task_struct_t* next)
;  RDI = prev
;  RSI = next
; ============================================================================
cpu_switch_to:
    ; 1. Save callee-saved registers of prev onto prev's current stack
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    pushfq

    ; 2. Save current RSP to prev->context.rsp (offset 216)
    mov [rdi + 216], rsp

    ; 3. Switch RSP to next->context.rsp (offset 216)
    mov rsp, [rsi + 216]

    ; 4. Update TSS.RSP0 to next->kernel_stack_top (offset 240)
    push rdi
    push rsi
    mov rdi, [rsi + 240]    ; next->kernel_stack_top
    test rdi, rdi
    jz .skip_tss
    call gdt_set_kernel_stack
.skip_tss:
    pop rsi
    pop rdi

    ; 5. Check if address spaces differ
    mov eax, [rdi + 768]    ; prev->address_space
    mov edx, [rsi + 768]    ; next->address_space
    cmp eax, edx
    je .same_addrsp

    push rdi
    push rsi
    mov edi, edx
    call paging_space_switch
    pop rsi
    pop rdi

.same_addrsp:
    ; 6. Restore callee-saved registers for next
    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; ============================================================================
;  kthread_entry_stub
;  When a new kernel thread is scheduled for the first time, cpu_switch_to
;  pops its initial registers and rets here.
;  R12 = int (*fn)(void*)
;  R13 = void* arg
; ============================================================================
kthread_entry_stub:
    sti                     ; Enable interrupts in new kernel thread
    mov rdi, r13            ; Pass arg as first parameter
    call r12                ; Call the thread function

    ; When function returns, exit the task with return value in RAX
    mov rdi, rax
    call task_exit

.halt:
    cli
    hlt
    jmp .halt
