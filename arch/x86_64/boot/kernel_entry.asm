[bits 32]

section .mb2hdr
align 8
mb2_header_start:
    dd 0xE85250D6                           ; Multiboot2 Magic
    dd 0                                    ; Architecture: 0 = i386 32-bit
    dd mb2_header_end - mb2_header_start    ; Header length
    dd -(0xE85250D6 + 0 + (mb2_header_end - mb2_header_start)) ; Checksum

    ; Framebuffer Request Tag
    align 8
    dw 5                                    ; MULTIBOOT_HEADER_TAG_FRAMEBUFFER
    dw 0                                    ; Flags
    dd 20                                   ; Size
    dd 1024                                 ; Width
    dd 768                                  ; Height
    dd 32                                   ; Depth

    ; End Tag
    align 8
    dw 0                                    ; Type = 0 (End)
    dw 0                                    ; Flags = 0
    dd 8                                    ; Size = 8
mb2_header_end:

section .text
extern kmain
extern bootinfo_init
extern __bss_start
extern __bss_end
extern __heap_start

global _start
_start:
    cli
    cld

    ; Stash Multiboot2 magic and MBI physical address
    mov [mb2_magic_saved], eax
    mov [mb2_mbi_saved], ebx

    ; Debug COM1 port notification
    mov dx, 0x3F8
    mov al, 'P'                             ; 'P' = Pulse Loader handoff received
    out dx, al

    ; ─────────────────────────────────────────────────────────────────────────
    ; Construct Early 4-Level Paging (PML4 -> PDPT -> 4x PDs -> 4 GiB @ 2MiB pages)
    ; ─────────────────────────────────────────────────────────────────────────
    ; PML4[0]   -> Identity map 0..4 GiB
    ; PML4[256] -> HHDM map (0xFFFF800000000000) 0..4 GiB
    ; PML4[511] -> Higher Half map (0xFFFFFFFF80000000)

    mov eax, early_pdpt
    or eax, 0x03                            ; Present + Writable
    mov [early_pml4 + 0], eax               ; PML4[0]
    mov [early_pml4 + 256 * 8], eax         ; PML4[256] (0xFFFF800000000000 HHDM)
    mov [early_pml4 + 511 * 8], eax         ; PML4[511] (0xFFFFFFFF80000000)

    ; Map 4 Page Directories into PDPT (covers 4 GiB)
    mov eax, early_pd0
    or eax, 0x03
    mov [early_pdpt + 0 * 8], eax           ; PDPT[0] (0..1 GiB)
    mov [early_pdpt + 510 * 8], eax         ; PDPT[510] (-2 GiB for Higher Half)

    mov eax, early_pd1
    or eax, 0x03
    mov [early_pdpt + 1 * 8], eax           ; PDPT[1] (1..2 GiB)
    mov [early_pdpt + 511 * 8], eax         ; PDPT[511] (-1 GiB for Higher Half)

    mov eax, early_pd2
    or eax, 0x03
    mov [early_pdpt + 2 * 8], eax           ; PDPT[2] (2..3 GiB)

    mov eax, early_pd3
    or eax, 0x03
    mov [early_pdpt + 3 * 8], eax           ; PDPT[3] (3..4 GiB)

    ; Populate 2048 x 2 MiB huge page entries (2048 * 2 MiB = 4096 MiB = 4 GiB)
    mov edi, early_pd0
    xor ecx, ecx
.map_4gib:
    mov eax, ecx
    shl eax, 21                             ; ecx * 2 MiB
    or eax, 0x83                            ; Present + Writable + Huge Page (2MiB)
    mov [edi], eax
    mov dword [edi + 4], 0
    add edi, 8
    inc ecx
    cmp ecx, 2048
    jb .map_4gib

    ; Load CR3 with PML4
    mov eax, early_pml4
    mov cr3, eax

    ; Enable CR4.PAE (Bit 5)
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax

    ; Enable Long Mode in EFER MSR (0xC0000080, Bit 8 LME)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr

    ; Load 64-bit GDT
    lgdt [gdt64_desc]

    ; Enable Paging (CR0.PG bit 31) and Protected Mode (CR0.PE bit 0)
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax

    ; Far jump to enter 64-bit Long Mode
    jmp 0x08:long_mode_entry

[bits 64]
long_mode_entry:
    ; Set up 64-bit segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; Debug COM1 port notification
    mov dx, 0x3F8
    mov al, 'L'                             ; 'L' = Long Mode Active
    out dx, al

    ; Switch to high-half kernel stack
    lea rsp, [rel boot_stack_top]
    and rsp, -16

    ; Zero BSS
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor rax, rax
    shr rcx, 3
    rep stosq

    ; Pass MBI pointer to bootinfo_init
    mov edi, [rel mb2_mbi_saved]
    call bootinfo_init

    ; Start the kernel
    call kmain

.hang:
    cli
    hlt
    jmp .hang

; Resume continuation for user tasks
global kernel_resume_trampoline
extern kconsole_resume
kernel_resume_trampoline:
    cli
    lea rsp, [rel boot_stack_top]
    and rsp, -16
    cld
    call kconsole_resume
.hang2:
    cli
    hlt
    jmp .hang2

section .data
align 8
mb2_magic_saved: dd 0
mb2_mbi_saved:   dd 0

align 8
gdt64:
    dq 0x0000000000000000                   ; Null Descriptor
    dq 0x00AF9A000000FFFF                   ; 0x08: 64-bit Code (L=1, D=0, Exec/Read)
    dq 0x00CF92000000FFFF                   ; 0x10: 64-bit Data (Read/Write)
gdt64_desc:
    dw gdt64_desc - gdt64 - 1
    dq gdt64

align 4096
early_pml4: times 4096 db 0
early_pdpt: times 4096 db 0
early_pd0:  times 4096 db 0
early_pd1:  times 4096 db 0
early_pd2:  times 4096 db 0
early_pd3:  times 4096 db 0

section .bss
align 16
global boot_stack_top
global boot_stack_bottom
boot_stack_bottom:
    resb 524288
boot_stack_top:
