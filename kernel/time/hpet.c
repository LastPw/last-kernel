#include "hpet.h"
#include "hw_topology.h"
#include "io.h"
#include "serial.h"
#include "string.h"

/* HPET is typically at a fixed MMIO address found through ACPI HPET table.
   On most systems this is at 0xFED00000, but we verify through ACPI. */

static hpet_status_t g_hpet;
static volatile u64* hpet_reg(u32 offset) {
    return (volatile u64*)(g_hpet.base_addr + offset);
}

static u64 hpet_read64(u32 offset) {
    return *hpet_reg(offset);
}

static void hpet_write64(u32 offset, u64 val) {
    *hpet_reg(offset) = val;
}

int hpet_available(void) {
    return g_hpet.available ? 1 : 0;
}

void hpet_status(hpet_status_t* out) {
    if (out) *out = g_hpet;
}

u64 hpet_read_counter(void) {
    return hpet_read64(HPET_MAIN_CNT);
}

int hpet_enable(void) {
    if (!g_hpet.available) return -1;
    u64 cfg = hpet_read64(HPET_GEN_CONFIG);
    cfg |= HPET_CFG_ENABLE;
    /* Do NOT set HPET_CFG_LEG_RT_IRQ: we never program an HPET comparator
       for the legacy route, and the legacy-replacement bit steals the PIT's
       IRQ0 input from the PIC.  Leaving it clear keeps the PIT (our only
       timer source, since the LAPIC is unavailable) driving IRQ0. */
    cfg &= ~HPET_CFG_LEG_RT_IRQ;
    hpet_write64(HPET_GEN_CONFIG, cfg);

    serial_writeln("[HPET] enabled");
    return 0;
}

void hpet_disable(void) {
    if (!g_hpet.available) return;
    u64 cfg = hpet_read64(HPET_GEN_CONFIG);
    cfg &= ~(HPET_CFG_ENABLE | HPET_CFG_LEG_RT_IRQ);
    hpet_write64(HPET_GEN_CONFIG, cfg);
    serial_writeln("[HPET] disabled");
}

int hpet_timer_enable(u8 tn, u32 period_us, u8 legacy_irq) {
    if (!g_hpet.available || tn >= g_hpet.timer_count) return -1;

    u64 tn_cfg = hpet_read64(HPET_TN_CONFIG(tn));

    /* If legacy replacement is active, timer 0 → IRQ0, timer 1 → IRQ8 */
    if (legacy_irq && g_hpet.legacy_replace && tn < 2) {
        tn_cfg &= ~HPET_TN_INT_ROUTE_MASK;
    } else if (!g_hpet.legacy_replace || tn >= 2) {
        /* When legacy replacement is not active, we need to set the routing.
           For simplicity, we rely on the HPET's default routing. */
        tn_cfg &= ~HPET_TN_INT_ROUTE_MASK;
    }

    /* Configure as periodic if period specified */
    if (period_us > 0) {
        /* Compute comparator value = period_in_fs / period_fs_per_tick */
        u64 period_fs = (u64)period_us * 1000000ull;
        u64 cmp = period_fs / g_hpet.period_fs;

        tn_cfg |= HPET_TN_PERIODIC;
        tn_cfg |= HPET_TN_ENABLE;
        tn_cfg |= HPET_TN_TYPE_LEVEL;  /* Level-triggered for legacy */
        tn_cfg &= ~HPET_TN_32MODE;     /* Use 64-bit mode */

        hpet_write64(HPET_TN_CONFIG(tn), tn_cfg);
        hpet_write64(HPET_TN_CMP(tn), cmp);

        /* For periodic mode, set the accumulator (timer value) = cmp */
        hpet_write64(HPET_TN_CONFIG(tn), tn_cfg | HPET_TN_VAL_SET);
        hpet_write64(HPET_TN_CMP(tn), cmp);
    } else {
        /* One-shot mode */
        tn_cfg &= ~HPET_TN_PERIODIC;
        tn_cfg |= HPET_TN_ENABLE;
        hpet_write64(HPET_TN_CONFIG(tn), tn_cfg);
    }

    serial_write("[HPET] timer ");
    serial_write_dec(tn);
    serial_write(" enabled, period=");
    serial_write_dec(period_us);
    serial_writeln(" us");
    return 0;
}

void hpet_timer_disable(u8 tn) {
    if (!g_hpet.available || tn >= g_hpet.timer_count) return;
    u64 tn_cfg = hpet_read64(HPET_TN_CONFIG(tn));
    tn_cfg &= ~(HPET_TN_ENABLE | HPET_TN_PERIODIC);
    hpet_write64(HPET_TN_CONFIG(tn), tn_cfg);
}

/* Read current time in nanoseconds since boot */
u64 hpet_read_ns(void) {
    if (!g_hpet.available) return 0;
    u64 counter = hpet_read_counter();
    /* time = counter * period_fs / 1000000 = counter * period_fs / 10^6 ns */
    /* Use 64-bit intermediate: (counter * period_fs) / 1000000 */
    /* But period_fs is small (typically ~10^5 = 100K), counter up to ~10^12,
       so counter * period_fs ~ 10^17 which fits in u64. */
    return (counter * (u64)g_hpet.period_fs) / 1000000ull;
}

/* Busy-wait for specified duration (for calibration) */
void hpet_busy_wait_ns(u32 ns) {
    u64 start = hpet_read_ns();
    u64 target = start + (u64)ns;
    while (hpet_read_ns() < target) {
        __asm__ __volatile__("pause");
    }
}

void hpet_busy_wait_us(u32 us) {
    hpet_busy_wait_ns(us * 1000u);
}

void hpet_busy_wait_ms(u32 ms) {
    hpet_busy_wait_ns(ms * 1000000u);
}

/* Find the HPET base address from the ACPI HPET table via hw_topology,
   which parsed the tables from the bootloader-reported RSDP.  A raw EBDA
   scan is impossible in long mode: the 0xE0000 window is not mapped by
   Limine, so any probe there triple-faults. */
#define ACPI_MAX_ADDR 0x100000000ull
static int find_hpet_from_acpi(void) {
    u32 base = hw_topology_hpet_base();
    if (!base || base >= ACPI_MAX_ADDR) return -1;
    return (int)base;
}

void hpet_init(void) {
    serial_writeln("[HPET] init start");
    memset(&g_hpet, 0, sizeof(g_hpet));

    /* Try to find HPET through ACPI first */
    int acpi_result = find_hpet_from_acpi();

    /* The standard HPET base address is 0xFED00000. Most systems
       conform to this. If ACPI gave us a different address, use that. */
    u32 hpet_base = (u32)(acpi_result > 0 ? acpi_result : 0xFED00000u);

    /* Probe the HPET at the base address by reading the capabilities register */
    volatile u64* cap_reg = (volatile u64*)(hpet_base + HPET_GEN_CAP);
    u64 cap = *cap_reg;

    /* Check for valid HPET: the revision ID should be non-zero.
       The period (bit 63:32) should be non-zero (1 fs resolution minimum). */
    u8 rev = (u8)(cap & HPET_CAP_REV);
    u32 period = (u32)((cap >> 32) & 0xFFFFFFFFu);

    if (rev == 0 || period == 0) {
        serial_writeln("[HPET] not found at 0xFED00000");
        g_hpet.available = 0u;
        return;
    }

    /* Valid HPET found */
    g_hpet.base_addr = hpet_base;
    g_hpet.period_fs = period;
    
    /* Calculate frequency: period is in femtoseconds (10^-15 s).
       Frequency Hz = 10^15 / period_fs = 1,000,000,000,000,000 / period_fs
       Since these numbers are large, use 64-bit. */
    if (period > 0) {
        g_hpet.freq_hz = 1000000000000000ull / (u64)period;
    }

    /* Parse capabilities */
    g_hpet.timer_count = (u8)((cap & HPET_CAP_NUM_TIM_MASK) >> 8) + 1u;
    g_hpet.legacy_replace = (cap & HPET_CAP_LEG_RT) ? 1 : 0;
    g_hpet.counter_64bit = (cap & HPET_CAP_COUNT_SIZE) ? 1 : 0;

    g_hpet.available = 1u;

    serial_write("[HPET] found at 0x");
    serial_write_hex(hpet_base);
    serial_write(" period=");
    serial_write_dec(period);
    serial_write(" fs freq=");
    serial_write_dec((u32)(g_hpet.freq_hz / 1000000u));
    serial_writeln(" MHz");
    serial_write("[HPET] timers=");
    serial_write_dec(g_hpet.timer_count);
    serial_write(" legacy_route=");
    serial_write_dec(g_hpet.legacy_replace);
    serial_write(" counter64=");
    serial_write_dec(g_hpet.counter_64bit);
    serial_writeln("");

    /* Enable the HPET */
    hpet_enable();
}
