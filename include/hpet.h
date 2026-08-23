#ifndef HPET_H
#define HPET_H

#include "types.h"

/* HPET register offsets (from MMIO base) */
#define HPET_GEN_CAP       0x000u  /* General Capabilities (64-bit) */
#define HPET_GEN_CONFIG    0x010u  /* General Configuration (64-bit) */
#define HPET_INT_STAT      0x020u  /* Interrupt Status (64-bit) */
#define HPET_MAIN_CNT      0x0F0u  /* Main Counter Value (64-bit) */
#define HPET_TN_CONFIG(n)  (0x100u + (n) * 0x20u)  /* Timer n Config (64-bit) */
#define HPET_TN_CMP(n)     (0x108u + (n) * 0x20u)  /* Timer n Comparator (64-bit) */
#define HPET_TN_ROUTE(n)   (0x110u + (n) * 0x20u)  /* Timer n FSB Interrupt Route (64-bit) */

/* HPET_GEN_CAP bit fields */
#define HPET_CAP_PERIOD_MASK   0xFFFFFFFF00000000ull  /* Bit 63:32 = period in fs */
#define HPET_CAP_VENDOR_MASK   0x00000000FFFF0000ull
#define HPET_CAP_LEG_RT        0x0000000000008000ull  /* LegacyReplacement Route */
#define HPET_CAP_COUNT_SIZE    0x0000000000002000ull  /* 1=64-bit counter */
#define HPET_CAP_NUM_TIM_MASK  0x0000000000001F00ull  /* Number of timers */
#define HPET_CAP_REV           0x00000000000000FFull

/* HPET_GEN_CONFIG bit fields */
#define HPET_CFG_ENABLE        0x0000000000000001ull
#define HPET_CFG_LEG_RT_IRQ    0x0000000000000002ull

/* HPET_TN_CONFIG bit fields */
#define HPET_TN_TYPE_LEVEL     0x0000000000000002ull
#define HPET_TN_ENABLE         0x0000000000000004ull
#define HPET_TN_PERIODIC       0x0000000000000008ull
#define HPET_TN_PERIODIC_CAP   0x0000000000000010ull
#define HPET_TN_SIZE_CAP       0x0000000000000020ull  /* 1=32-bit, 0=64-bit */
#define HPET_TN_FSB_ENABLE     0x0000000000000040ull
#define HPET_TN_FSB_INT_DEL    0x0000000000000080ull
#define HPET_TN_INT_ROUTE_MASK 0x000000003E000000ull
#define HPET_TN_INT_TYPE       0x0000000000000100ull  /* 0=edge, 1=level */
#define HPET_TN_VAL_SET        0x0000000000000200ull  /* Timer value set bit */
#define HPET_TN_32MODE         0x0000000000000400ull  /* 32-bit mode */
#define HPET_TN_ROUTE_CAP_MASK 0xFFFFFFFF00000000ull

/* HPET IRQ routing */
#define HPET_IRQ_LEGACY_TIMER0  0  /* IRQ0 (PIT replacement) */
#define HPET_IRQ_LEGACY_TIMER1  8  /* IRQ8 (RTC replacement) */

/* Status */
typedef struct {
    u8  available;
    u8  timer_count;
    u8  legacy_replace;
    u8  counter_64bit;
    u32 base_addr;
    u32 period_fs;       /* Period in femtoseconds */
    u64 freq_hz;         /* Frequency in Hz */
} hpet_status_t;

/* Functions */
void hpet_init(void);
int  hpet_available(void);
void hpet_status(hpet_status_t* out);
u64  hpet_read_counter(void);
u64  hpet_read_ns(void);         /* Read time in nanoseconds */
void hpet_busy_wait_ns(u32 ns);
void hpet_busy_wait_us(u32 us);
void hpet_busy_wait_ms(u32 ms);
int  hpet_enable(void);
void hpet_disable(void);
int  hpet_timer_enable(u8 tn, u32 period_us, u8 legacy_irq);
void hpet_timer_disable(u8 tn);

#endif /* HPET_H */
