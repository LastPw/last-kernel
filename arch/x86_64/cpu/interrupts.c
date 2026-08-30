#include "interrupts.h"
#include "idt.h"
#include "io.h"
#include "timer.h"
#include "keyboard.h"
#include "mouse.h"
#include "kernel_api.h"
#include "core.h"
#include "serial.h"
#include "irq_sanitizer.h"
#include "audit.h"
#include "io_guard.h"
#include "syscall.h"
#include "kshield.h"
#include "apic.h"

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);
extern void isr128(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

static isr_t isr_handlers[32];
static isr_t irq_handlers[16];
static u32 total_irq_count = 0;
static u8 minimal_mode = 0u;
static u8 use_apic_mode = 0u;

static void lputc(char c) {
    kputc(c);
    serial_putc(c);
}

static void lputs(const char* s) {
    kputs(s);
    serial_write(s);
}

static void lprint_u32(u32 value) {
    char buf[16];
    int i = 0;

    if (value == 0) {
        lputc('0');
        return;
    }

    while (value > 0 && i < (int)sizeof(buf) - 1) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        lputc(buf[--i]);
    }
}

static void lprint_hex32(u32 value) {
    for (int i = 7; i >= 0; i--) {
        u8 nib = (u8)((value >> (i * 4)) & 0xF);
        lputc((char)(nib < 10 ? ('0' + nib) : ('A' + nib - 10)));
    }
}

static void lprint_hex64(u64 value) {
    for (int i = 15; i >= 0; i--) {
        u8 nib = (u8)((value >> (i * 4)) & 0xF);
        lputc((char)(nib < 10 ? ('0' + nib) : ('A' + nib - 10)));
    }
}

static void pic_remap(void) {
    u8 a1 = inb(0x21);
    u8 a2 = inb(0xA1);

    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();

    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();

    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();

    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();

    outb(0x21, a1);
    outb(0xA1, a2);
}

static void pic_send_eoi(u8 irq) {
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

static void pic_write_masks(u8 master_mask, u8 slave_mask) {
    outb(0x21, master_mask);
    outb(0xA1, slave_mask);
}

void interrupts_mask_all(void) {
    if (use_apic_mode && apic_is_available()) {
        apic_mask_all();
    }
    pic_write_masks(0xFFu, 0xFFu);
}

void interrupts_unmask_irq(u8 irq) {
    if (use_apic_mode && apic_is_available()) {
        apic_unmask_irq(irq);
        return;
    }
    u8 master = inb(0x21);
    u8 slave = inb(0xA1);
    if (irq < 8u) {
        master = (u8)(master & (u8)~(1u << irq));
    } else if (irq < 16u) {
        slave = (u8)(slave & (u8)~(1u << (irq - 8u)));
        master = (u8)(master & (u8)~(1u << 2u)); /* cascade */
    }
    pic_write_masks(master, slave);
}

void interrupts_mask_irq(u8 irq) {
    if (use_apic_mode && apic_is_available()) {
        apic_mask_irq(irq);
        return;
    }
    u8 master = inb(0x21);
    u8 slave = inb(0xA1);
    if (irq < 8u) {
        master = (u8)(master | (u8)(1u << irq));
    } else if (irq < 16u) {
        slave = (u8)(slave | (u8)(1u << (irq - 8u)));
    }
    pic_write_masks(master, slave);
}

void interrupts_enable(void) {
    __asm__ __volatile__("sti");
}

void interrupts_disable(void) {
    __asm__ __volatile__("cli");
}

void interrupts_set_minimal_mode(u8 enabled) {
    minimal_mode = enabled ? 1u : 0u;
}

void interrupts_set_apic_mode(u8 enabled) {
    use_apic_mode = enabled ? 1u : 0u;
    if (use_apic_mode) {
        serial_writeln("[INTR] APIC interrupt mode enabled");
    }
}

u8 interrupts_apic_mode(void) {
    return use_apic_mode;
}

void isr_register_handler(u8 n, isr_t handler) {
    if (n < 32) {
        isr_handlers[n] = handler;
    }
}

void irq_register_handler(u8 n, isr_t handler) {
    if (n < 16) {
        irq_handlers[n] = handler;
    }
}

void interrupts_init(void) {
    pic_remap();
    interrupts_mask_all();

    idt_set_gate(0,  (u64)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (u64)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (u64)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (u64)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (u64)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (u64)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (u64)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (u64)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (u64)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (u64)isr9,  0x08, 0x8E);
    idt_set_gate(10, (u64)isr10, 0x08, 0x8E);
    idt_set_gate(11, (u64)isr11, 0x08, 0x8E);
    idt_set_gate(12, (u64)isr12, 0x08, 0x8E);
    idt_set_gate(13, (u64)isr13, 0x08, 0x8E);
    idt_set_gate(14, (u64)isr14, 0x08, 0x8E);
    idt_set_gate(15, (u64)isr15, 0x08, 0x8E);
    idt_set_gate(16, (u64)isr16, 0x08, 0x8E);
    idt_set_gate(17, (u64)isr17, 0x08, 0x8E);
    idt_set_gate(18, (u64)isr18, 0x08, 0x8E);
    idt_set_gate(19, (u64)isr19, 0x08, 0x8E);
    idt_set_gate(20, (u64)isr20, 0x08, 0x8E);
    idt_set_gate(21, (u64)isr21, 0x08, 0x8E);
    idt_set_gate(22, (u64)isr22, 0x08, 0x8E);
    idt_set_gate(23, (u64)isr23, 0x08, 0x8E);
    idt_set_gate(24, (u64)isr24, 0x08, 0x8E);
    idt_set_gate(25, (u64)isr25, 0x08, 0x8E);
    idt_set_gate(26, (u64)isr26, 0x08, 0x8E);
    idt_set_gate(27, (u64)isr27, 0x08, 0x8E);
    idt_set_gate(28, (u64)isr28, 0x08, 0x8E);
    idt_set_gate(29, (u64)isr29, 0x08, 0x8E);
    idt_set_gate(30, (u64)isr30, 0x08, 0x8E);
    idt_set_gate(31, (u64)isr31, 0x08, 0x8E);
    idt_set_gate(128, (u64)isr128, 0x08, 0xEE);

    idt_set_gate(32, (u64)irq0,  0x08, 0x8E);
    idt_set_gate(33, (u64)irq1,  0x08, 0x8E);
    idt_set_gate(34, (u64)irq2,  0x08, 0x8E);
    idt_set_gate(35, (u64)irq3,  0x08, 0x8E);
    idt_set_gate(36, (u64)irq4,  0x08, 0x8E);
    idt_set_gate(37, (u64)irq5,  0x08, 0x8E);
    idt_set_gate(38, (u64)irq6,  0x08, 0x8E);
    idt_set_gate(39, (u64)irq7,  0x08, 0x8E);
    idt_set_gate(40, (u64)irq8,  0x08, 0x8E);
    idt_set_gate(41, (u64)irq9,  0x08, 0x8E);
    idt_set_gate(42, (u64)irq10, 0x08, 0x8E);
    idt_set_gate(43, (u64)irq11, 0x08, 0x8E);
    idt_set_gate(44, (u64)irq12, 0x08, 0x8E);
    idt_set_gate(45, (u64)irq13, 0x08, 0x8E);
    idt_set_gate(46, (u64)irq14, 0x08, 0x8E);
    idt_set_gate(47, (u64)irq15, 0x08, 0x8E);

    for (int i = 0; i < 32; i++) {
        isr_handlers[i] = 0;
    }
    for (int i = 0; i < 16; i++) {
        irq_handlers[i] = 0;
    }

    irq_register_handler(0, (isr_t)timer_on_irq);
    irq_register_handler(1, (isr_t)keyboard_on_irq);
    irq_register_handler(12, (isr_t)mouse_on_irq);
}

#include "self_heal.h"
#include "apk.h"

void isr_handler(registers_t* regs) {
    if (regs->int_no == 128u) {
        u32 cur_task = scheduler_current_task();
        kshield_save_checkpoint(cur_task, regs);
        syscall_on_interrupt(regs);
        return;
    }

    /* KShield Time-Travel & Memory Self-Healing Gate */
    if (regs->int_no < 32) {
        if (kshield_try_heal(regs)) {
            return; /* Exception neutralised, execution safely restored */
        }
        if (apk_handle_hardware_exception(regs->int_no, regs, 0)) {
            return; /* APK Micro-Domain fault safely isolated & rewound */
        }
    }

    if (regs->int_no < 32 && isr_handlers[regs->int_no]) {
        if ((regs->cs & 3u) == 3u) {
            serial_write("[isr] user exc int=");
            serial_write_dec(regs->int_no);
            serial_write(" rip=0x");
            serial_write_hex((u32)((unsigned long)regs->rip & 0xFFFFFFFFu));
            serial_putc('\n');
        }
        isr_handlers[regs->int_no](regs);
        return;
    }

    if (regs->int_no < 32) {
        serial_write("[isr] unhandled int=");
        serial_write_dec(regs->int_no);
        serial_write(" cs=");
        serial_write_dec(regs->cs);
        serial_write(" rip=0x");
        serial_write_hex((u32)((unsigned long)regs->rip & 0xFFFFFFFFu));
        serial_putc('\n');
        core_capture_trap(regs->int_no, regs->err_code, (u32)regs->rip, 0u);
        audit_log(AUDIT_EVT_EXCEPTION, regs->int_no, regs->err_code);
        lputs("EXCEPTION int=");
        lprint_u32(regs->int_no);
        lputs(" err=0x");
        lprint_hex32(regs->err_code);
        lputs(" rip=0x");
        lprint_hex64(regs->rip);
        lputc('\n');

        if (regs->int_no == 14) {
            u64 cr2 = 0;
            __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
            core_capture_trap(14u, regs->err_code, (u32)regs->rip, (u32)cr2);
            audit_log(AUDIT_EVT_PAGE_FAULT, (u32)cr2, (u32)regs->rip);
            lputs("PAGE_FAULT addr=0x");
            lprint_hex64(cr2);
            lputc('\n');
        }

        /* A bad instruction, divide error, GPF, or similar fault in ring 3
           belongs to the process, not to the kernel. Record it and retire
           only that task; the common ISR path will return through the clean
           kernel continuation selected by the scheduler. */
        {
            u32 task_id = scheduler_current_task();
            if (scheduler_task_is_user(task_id)) {
                paniclog_push_for_task(task_id, "user process exception",
                                       regs->int_no, regs->err_code, (u32)regs->rip,
                                       0u, timer_ticks(), 0u, 0u, 0u, 0u,
                                       0u, 0u);
                serial_write("[LastOS] user process terminated on exception task=");
                serial_write_dec(task_id);
                serial_putc('\n');
                (void)scheduler_kill_task(task_id);
                scheduler_schedule_from_irq(regs);
                return;
            }
        }

        panic("Unhandled CPU exception");
    }
}

void irq_handler(registers_t* regs) {
    u8 irq = (u8)(regs->int_no - 32);
    total_irq_count++;
    if ((total_irq_count & 0x3FFu) == 0u) {
        serial_write("[irq] count=");
        serial_write_dec(total_irq_count);
        serial_write(" int=");
        serial_write_dec(regs->int_no);
        serial_write(" irq=");
        serial_write_dec(irq);
        serial_putc('\n');
    }
    if (minimal_mode) {
        if (irq < 16 && irq_handlers[irq]) {
            irq_handlers[irq](regs);
        }
        if (use_apic_mode && apic_is_available()) {
            apic_lapic_eoi();
        } else if (irq < 16) {
            pic_send_eoi(irq);
        } else {
            outb(0x20, 0x20);
        }
        return;
    }

    io_guard_on_irq(irq, total_irq_count);

    if (irq >= 16) {
        core_capture_trap(regs->int_no, regs->err_code, (u32)regs->rip, irq);
        audit_log(AUDIT_EVT_TASK_VIOLATION, irq, regs->int_no);
        if (use_apic_mode && apic_is_available()) {
            apic_lapic_eoi();
        } else {
            outb(0x20, 0x20);
        }
        return;
    }

    if (kshield_on_irq(irq, total_irq_count) != 0) {
        audit_log(AUDIT_EVT_IRQ_RATE_LIMIT, irq, total_irq_count);
        if (use_apic_mode && apic_is_available()) {
            apic_lapic_eoi();
        } else {
            pic_send_eoi(irq);
        }
        return;
    }

    if (!irq_sanitizer_allow(irq, total_irq_count)) {
        audit_log(AUDIT_EVT_IRQ_RATE_LIMIT, irq, total_irq_count);
        if (use_apic_mode && apic_is_available()) {
            apic_lapic_eoi();
        } else {
            pic_send_eoi(irq);
        }
        return;
    }

    if (irq < 16 && irq_handlers[irq]) {
        if (irq == 0u && (total_irq_count & 0x3FFu) == 0u) {
            serial_write("[irq] calling timer_on_irq via handler[0]=");
            serial_write_hex((u32)((unsigned long)irq_handlers[0] & 0xFFFFFFFFu));
            serial_putc('\n');
        }
        irq_handlers[irq](regs);
    } else if (irq == 0u && (total_irq_count & 0x3FFu) == 0u) {
        serial_write("[irq] handler[0] is NULL! h0=");
        serial_write_hex((u32)((unsigned long)irq_handlers[0] & 0xFFFFFFFFu));
        serial_putc('\n');
    }

    if (use_apic_mode && apic_is_available()) {
        apic_lapic_eoi();
    } else if (irq < 16) {
        pic_send_eoi(irq);
    }
}

u32 interrupts_total_count(void) {
    return total_irq_count;
}
