#ifndef APIC_H
#define APIC_H

#include "types.h"
#include "idt.h"

/* ======================== LAPIC Registers (MMIO offsets) ======================== */
#define LAPIC_ID         0x020u  /* Local APIC ID register */
#define LAPIC_VER        0x030u  /* Version register */
#define LAPIC_TPR        0x080u  /* Task Priority Register */
#define LAPIC_APR        0x090u  /* Arbitration Priority */
#define LAPIC_PPR        0x0A0u  /* Processor Priority */
#define LAPIC_EOI        0x0B0u  /* End Of Interrupt */
#define LAPIC_RRD        0x0C0u  /* Remote Read */
#define LAPIC_LDR        0x0D0u  /* Logical Destination */
#define LAPIC_DFR        0x0E0u  /* Destination Format */
#define LAPIC_SVR        0x0F0u  /* Spurious Interrupt Vector */
#define LAPIC_ISR_BASE   0x100u  /* In-Service Register (bits 0-255) */
#define LAPIC_TMR_BASE   0x180u  /* Trigger Mode Register */
#define LAPIC_IRR_BASE   0x200u  /* Interrupt Request Register */
#define LAPIC_ESR        0x280u  /* Error Status */
#define LAPIC_ICR_LOW    0x300u  /* Interrupt Command (low) */
#define LAPIC_ICR_HIGH   0x310u  /* Interrupt Command (high) */
#define LAPIC_TIMER      0x320u  /* LVT Timer Register */
#define LAPIC_THERMAL    0x330u  /* LVT Thermal Sensor */
#define LAPIC_PERF       0x340u  /* LVT Performance Counter */
#define LAPIC_LINT0      0x350u  /* LVT LINT0 */
#define LAPIC_LINT1      0x360u  /* LVT LINT1 */
#define LAPIC_ERROR      0x370u  /* LVT Error */
#define LAPIC_TIMER_INIT 0x380u  /* Timer Initial Count */
#define LAPIC_TIMER_CUR  0x390u  /* Timer Current Count */
#define LAPIC_TIMER_DIV  0x3E0u  /* Timer Divide Configuration */

/* LAPIC SVR flags */
#define LAPIC_SVR_ENABLE       0x100u
#define LAPIC_SVR_FOCUS        0x200u

/* LAPIC ICR flags */
#define LAPIC_ICR_FIXED        0x00000u
#define LAPIC_ICR_LOWEST       0x00100u
#define LAPIC_ICR_SMI          0x00200u
#define LAPIC_ICR_NMI          0x00400u
#define LAPIC_ICR_INIT         0x00500u
#define LAPIC_ICR_STARTUP      0x00600u
#define LAPIC_ICR_PHYSICAL     0x00000u
#define LAPIC_ICR_LOGICAL      0x00800u
#define LAPIC_ICR_ASSERT       0x04000u
#define LAPIC_ICR_DEASSERT     0x00000u
#define LAPIC_ICR_EDGE         0x00000u
#define LAPIC_ICR_LEVEL        0x08000u
#define LAPIC_ICR_DELIVERY     0x01000u  /* Delivery status bit */

/* LAPIC Timer flags */
#define LAPIC_TIMER_MASK       0x10000u
#define LAPIC_TIMER_PERIODIC   0x20000u
#define LAPIC_TIMER_ONESHOT    0x00000u
#define LAPIC_TIMER_TSC_DEADLINE 0x40000u

/* LAPIC Timer divider values */
#define LAPIC_TIMER_DIV_1      0x0Bu
#define LAPIC_TIMER_DIV_2      0x00u
#define LAPIC_TIMER_DIV_4      0x01u
#define LAPIC_TIMER_DIV_8      0x02u
#define LAPIC_TIMER_DIV_16     0x03u
#define LAPIC_TIMER_DIV_32     0x08u
#define LAPIC_TIMER_DIV_64     0x09u
#define LAPIC_TIMER_DIV_128    0x0Au

/* LVT flags */
#define LAPIC_LVT_MASK         0x10000u
#define LAPIC_LVT_TRIGGER      0x08000u
#define LAPIC_LVT_REMOTE       0x04000u
#define LAPIC_LVT_PENDING      0x02000u

/* ======================== I/O APIC Registers ======================== */
#define IOAPIC_IOREGSEL        0x00u  /* I/O APIC Register Select */
#define IOAPIC_IOWIN           0x10u  /* I/O APIC Window */

#define IOAPIC_ID              0x00u  /* IO APIC Identification */
#define IOAPIC_VER             0x01u  /* IO APIC Version */
#define IOAPIC_ARB             0x02u  /* IO APIC Arbitration */
#define IOAPIC_REDIR_BASE      0x10u  /* Redirection Table (base index) */

/* I/O APIC Redirection Entry (lower 32 bits) flags */
#define IOAPIC_REDIR_MASK      0x00010000u  /* 1 = masked */
#define IOAPIC_REDIR_TRIGGER   0x00008000u  /* 1 = level-triggered */
#define IOAPIC_REDIR_REMOTE    0x00004000u  /* Remote IRR */
#define IOAPIC_REDIR_POLARITY  0x00002000u  /* 1 = low-active */
#define IOAPIC_REDIR_DELIVERY  0x00001000u  /* Delivery status */
#define IOAPIC_REDIR_DESTMOD   0x00000800u  /* 1 = logical dest */
#define IOAPIC_REDIR_FIXED     0x00000000u
#define IOAPIC_REDIR_LOWEST    0x00000100u
#define IOAPIC_REDIR_SMI       0x00000200u
#define IOAPIC_REDIR_NMI       0x00000400u
#define IOAPIC_REDIR_INIT      0x00000500u
#define IOAPIC_REDIR_EXTINT    0x00000700u

/* MADT entry types */
#define MADT_TYPE_LAPIC        0x00u
#define MADT_TYPE_IOAPIC       0x01u
#define MADT_TYPE_ISO          0x02u  /* Interrupt Source Override */
#define MADT_TYPE_NMI_SRC      0x03u  /* NMI Source */
#define MADT_TYPE_LAPIC_NMI    0x04u  /* Local APIC NMI */
#define MADT_TYPE_LAPIC_ADDR   0x05u  /* Local APIC Address Override */
#define MADT_TYPE_IOAPIC_SRC   0x06u  /* I/O SAPIC */
#define MADT_TYPE_LSAPIC       0x07u  /* Local SAPIC */
#define MADT_TYPE_PLATFORM     0x08u  /* Platform Interrupt */
#define MADT_TYPE_X2APIC       0x09u  /* x2APIC */
#define MADT_TYPE_X2APIC_NMI   0x0Au  /* x2APIC NMI */

/* Maximum number of I/O APICs we support */
#define APIC_MAX_IOAPICS       4u
#define APIC_MAX_REDIR_ENTRIES 48u

/* I/O APIC redirection entry */
typedef struct {
    u32 vector    : 8;
    u32 deliv_mode: 3;
    u32 dest_mode : 1;
    u32 deliv_status : 1;
    u32 polarity  : 1;
    u32 remote_irr: 1;
    u32 trigger   : 1;
    u32 mask      : 1;
    u32 reserved  : 15;
} __attribute__((packed)) ioapic_redir_low_t;

typedef struct {
    u32 reserved  : 24;
    u32 dest      : 8;
} __attribute__((packed)) ioapic_redir_high_t;

/* I/O APIC instance */
typedef struct {
    u32 base_addr;
    u32 gsi_base;     /* Global System Interrupt base */
    u32 max_redirs;
    u32 id;
} ioapic_instance_t;

/* APIC status */
typedef struct {
    u8  available;       /* APIC is present and initialized */
    u8  lapic_enabled;   /* LAPIC is enabled */
    u8  ioapic_count;    /* Number of I/O APICs */
    u8  timer_mode;      /* 0=PIT, 1=LAPIC timer, 2=HPET */
    u32 lapic_addr;      /* LAPIC MMIO base */
    u32 lapic_freq_khz;  /* LAPIC timer frequency in KHz (approx) */
    u32 bsp_apic_id;     /* BSP's APIC ID */
} apic_status_t;

/* ======================== Function Declarations ======================== */

/* Initialization */
void apic_init(void);
int  apic_is_available(void);
void apic_status(apic_status_t* out);

/* LAPIC access */
u32  apic_lapic_read(u32 reg);
void apic_lapic_write(u32 reg, u32 val);
u32  apic_lapic_id(void);
void apic_lapic_eoi(void);
void apic_lapic_enable(void);
void apic_lapic_disable(void);

/* I/O APIC access */
u32  apic_ioapic_read(u32 ioapic_idx, u32 reg);
void apic_ioapic_write(u32 ioapic_idx, u32 reg, u32 val);
int  apic_ioapic_count(void);
int  apic_ioapic_info(u32 idx, ioapic_instance_t* out);

/* Interrupt redirection */
void apic_redirect_irq(u8 irq, u8 vector, u32 dest_apic_id, u8 trigger_mode);
void apic_mask_irq(u8 irq);
void apic_unmask_irq(u8 irq);
void apic_mask_all(void);

/* LAPIC timer */
int  apic_timer_init(u32 frequency_hz, u8 vector);
int  apic_timer_oneshot(u32 count);
int  apic_timer_stop(void);
u32  apic_timer_read_count(void);
u32  apic_timer_calibrate(void);
u32  apic_timer_freq_khz(void);
int  apic_timer_tsc_deadline(u8 vector);

/* Utility */
void apic_send_ipi(u32 apic_id, u32 icr_low);
void apic_send_init_ipi(u32 apic_id);
void apic_send_startup_ipi(u32 apic_id, u8 vector);
void apic_dump_status(void);

#endif /* APIC_H */
