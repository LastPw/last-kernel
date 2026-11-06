#include "gdt.h"
#include "types.h"
#include "string.h"
#include "serial.h"

/* x86_64 GDT.
 * Layout keeps the legacy selector values intact so existing kernel code
 * (scheduler, syscall, task_protect) that compares regs->cs / regs->ss
 * against 0x08/0x10/0x1B/0x23 keeps working:
 *   0: null
 *   1: kernel code  0x08  (L=1)
 *   2: kernel data  0x10
 *   3: user  code   0x1B  (L=1, DPL=3)
 *   4: user  data   0x23  (DPL=3)
 *   5+6: TSS descriptor (16 bytes, selector 0x28) — rsp0 for ring3->ring0
 */

typedef struct {
    u16 limit_low;
    u16 base_low;
    u8 base_middle;
    u8 access;
    u8 granularity;
    u8 base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    u16 limit;
    u64 base;
} __attribute__((packed)) gdt_ptr_t;

/* TSS64: rsp0 is where the CPU loads RSP when an interrupt comes in from
   ring 3 (to avoid trusting the user stack).  ist[] is unused. */
typedef struct __attribute__((packed)) {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[7];
    u32 reserved2;
    u32 reserved3;
    u16 reserved4;
    u16 iomap_base;
} tss64_t;

/* The 16-byte TSS descriptor: two 8-byte halves, base split across 3 low
   bytes + 4 upper bytes, exactly as the CPU defines it. */
typedef struct __attribute__((packed)) {
    u16 limit_low;
    u16 base_low;
    u8 base_middle;
    u8 access;
    u8 granularity;
    u8 base_high;
    u32 base_upper;
    u32 reserved;
} tss_desc_t;

#define GDT_KCODE 0x08u
#define GDT_KDATA 0x10u
#define GDT_UCODE 0x1Bu
#define GDT_UDATA 0x23u
#define GDT_TSS   0x28u

extern void gdt_flush(u64 gdt_ptr_addr);
extern void ltr_tss(void);
void gdt_set_kernel_stack(u64 rsp0);

static gdt_entry_t gdt[7];
static gdt_ptr_t gp;
static tss64_t tss;

static void gdt_set_gate(int num, u32 base, u32 limit, u8 access, u8 gran) {
    if (num < 0 || num >= (int)(sizeof(gdt) / sizeof(gdt[0]))) {
        return;
    }
    gdt[num].base_low = (u16)(base & 0xFFFF);
    gdt[num].base_middle = (u8)((base >> 16) & 0xFF);
    gdt[num].base_high = (u8)((base >> 24) & 0xFF);

    gdt[num].limit_low = (u16)(limit & 0xFFFF);
    gdt[num].granularity = (u8)((limit >> 16) & 0x0F);
    gdt[num].granularity |= (u8)(gran & 0xF0);
    gdt[num].access = access;
}

extern u8 boot_stack_top[];

void gdt_init(void) {
    serial_writeln("[LastOS] gdt64 init");
    memset(&tss, 0, sizeof(tss));
    tss.iomap_base = (u16)sizeof(tss);
    /* Ring-3->ring-0 entry stack: the high-half boot stack from
       kernel_entry.asm (512 KiB, well past .bss).  The old 0x03E00000 low
       physical address predates the higher-half move. */
    tss.rsp0 = (u64)(uintptr_t)boot_stack_top;

    memset(gdt, 0, sizeof(gdt));

    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF); /* kcode L=1 */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xAF); /* kdata */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xAF); /* ucode L=1 DPL3 */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xAF); /* udata   DPL3 */

    /* TSS descriptor across gates 5+6 (selector 0x28). */
    tss_desc_t* td = (tss_desc_t*)&gdt[5];
    td->limit_low = (u16)(sizeof(tss64_t) - 1);
    u64 b = (u64)&tss;
    td->base_low = (u16)(b & 0xFFFF);
    td->base_middle = (u8)((b >> 16) & 0xFF);
    td->base_high = (u8)((b >> 24) & 0xFF);
    td->base_upper = (u32)(b >> 32);
    td->access = 0x89;
    td->granularity = 0x00;
    td->reserved = 0;

    gp.limit = (u16)(sizeof(gdt) - 1);
    gp.base = (u64)&gdt;

    gdt_flush((u64)&gp);

    ltr_tss();

    /* Enable SSE for ring-3 user binaries (built -O2, they emit SSE).
       The kernel itself is built -mno-sse, so the XMM state that now
       exists in the CPU is purely the interrupted user task's; the
       scheduler fxsaverestores it around context switches. */
    u64 cr4 = 0;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ull << 9) | (1ull << 10); /* OSFXSR | OSXMMEXCPT */
    __asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4) : "memory");

    u32 eax = 0u, ebx = 0u, ecx = 0u, edx = 0u;
    __asm__ __volatile__("cpuid"
                         : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         : "a"(1u));

    /* XSAVE bit 26, AVX bit 28: CR4.OSXSAVE (bit 18) MUST be set before executing xgetbv/xsetbv */
    if (ecx & (1u << 26)) {
        cr4 |= (1ull << 18); /* CR4.OSXSAVE */
        __asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4) : "memory");

        if (ecx & (1u << 28)) { /* AVX */
            u32 xlo = 0u, xhi = 0u;
            __asm__ __volatile__("xgetbv" : "=a"(xlo), "=d"(xhi) : "c"(0u));
            u64 xcr0 = ((u64)xhi << 32) | xlo;
            xcr0 |= 0x7u; /* XCR0.X87 | XCR0.SSE | XCR0.AVX */
            __asm__ __volatile__("xsetbv"
                                 : : "c"(0u), "a"((u32)xcr0), "d"((u32)(xcr0 >> 32)));
        }
    }
}

void gdt_set_kernel_stack(u64 rsp0) {
    tss.rsp0 = rsp0;
}
void* gdt_descriptor(void) { return (void*)&gp; }
