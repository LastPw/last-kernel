[bits 64]

global pku_is_supported
global pku_enable_cr4
global pku_read_pkru
global pku_write_pkru

section .text

; pku_is_supported(): checks CPUID.(EAX=7,ECX=0):ECX[PKU bit 3]
pku_is_supported:
    push rbx
    mov eax, 7
    xor ecx, ecx
    cpuid
    bt ecx, 3
    setc al
    movzx eax, al
    pop rbx
    ret

; pku_enable_cr4(): sets CR4.PKE (bit 22)
pku_enable_cr4:
    mov rax, cr4
    bts rax, 22
    mov cr4, rax
    ret

; pku_read_pkru(): returns 32-bit PKRU value
pku_read_pkru:
    xor ecx, ecx
    ; rdpkru: 0x0F 0x01 0xEE
    db 0x0F, 0x01, 0xEE
    ret

; pku_write_pkru(u32 val): writes EDI to PKRU
pku_write_pkru:
    mov eax, edi
    xor ecx, ecx
    xor edx, edx
    ; wrpkru: 0x0F 0x01 0xEF
    db 0x0F, 0x01, 0xEF
    ret
