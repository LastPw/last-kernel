#include "idt.h"
#include "serial.h"

typedef struct {
    u16 base_lo;
    u16 sel;
    u8 always0;
    u8 flags;
    u16 base_mid;
    u32 base_hi;
    u32 reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    u16 limit;
    u64 base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt_entries[256];
static idt_ptr_t idt_ptr;

static void idt_load(u64 idt_ptr_addr) {
    __asm__ __volatile__("lidt (%0)" : : "r"(idt_ptr_addr));
}

void idt_set_gate(u8 num, u64 base, u16 sel, u8 flags) {
    idt_entries[num].base_lo = (u16)(base & 0xFFFF);
    idt_entries[num].base_mid = (u16)((base >> 16) & 0xFFFF);
    idt_entries[num].base_hi = (u32)((base >> 32) & 0xFFFFFFFF);
    idt_entries[num].sel = sel;
    idt_entries[num].always0 = 0;
    idt_entries[num].flags = flags;
    idt_entries[num].reserved = 0;
}

void idt_init(void) {
    idt_ptr.limit = (u16)(sizeof(idt_entry_t) * 256 - 1);
    idt_ptr.base = (u64)&idt_entries;

    for (int i = 0; i < 256; i++) {
        idt_set_gate((u8)i, 0, 0, 0);
    }

    idt_load((u64)&idt_ptr);
}