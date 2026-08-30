[BITS 64]
global _start
extern main
extern exit

section .text
_start:
    xor rbp, rbp
    
    ; Pop argc and argv from stack
    pop rdi             ; argc
    mov rsi, rsp        ; argv
    
    ; Align stack to 16 bytes
    and rsp, -16
    
    call main
    
    ; Call exit with main return value
    mov rdi, rax
    call exit
    
.halt:
    mov rax, 18         ; SYS_EXIT
    xor rbx, rbx
    int 0x80
    jmp .halt
