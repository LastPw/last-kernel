#ifndef IDT_H
#define IDT_H

#include "types.h"

/* x86_64 interrupt frame as pushed by cpu/interrupt_stubs.asm, plus the
   vector/err that the stub adds.  Field order matches the SysV ABI layout of
   the shared registers_t used across the kernel.

   rbp is saved first (lowest offset).  iretq restores only rip/cs/rflags/
   rsp/ss, so the stub pops rbp from the (possibly rewritten) frame as the
   last step before iretq; a kernel task resumed mid-function after a long
   detour (e.g. a killed user task) otherwise keeps whatever rbp the detour
   left in the CPU and crashes on its epilogue. */
typedef struct {
    u64 rax, rbx, rcx, rdx, rsi, rdi;
    u64 r8, r9, r10, r11, r12, r13, r14, r15;
    u64 rbp;
    u64 int_no, err_code;
    u64 rip, cs, rflags, rsp, ss;
} registers_t;

void idt_init(void);
void idt_set_gate(u8 num, u64 base, u16 sel, u8 flags);

void* idt_descriptor(void);
#endif