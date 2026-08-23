#include "apic.h"
#include "bootinfo.h"
#include "hw_topology.h"
#include "io.h"
#include "timer.h"
#include "serial.h"
#include "string.h"
#include "paging.h"

/* Long mode has no identity map: MMIO (LAPIC/IOAPIC) and ACPI tables live
   at physical addresses that Limine maps through the HHDM only for selected
   memmap types.  Map the page on demand before the first access. */
static volatile u32* mmio_map(u64 phys) {
    u64 va = phys + g_hhdm_offset;
    if (paging_virt_to_phys(va) == 0) {
        if (paging_map_page(va, phys, PAGE_FLAG_PRESENT | PAGE_FLAG_RW) != 0) return 0;
    }
    return (volatile u32*)va;
}

/* ======================== Global State ======================== */
static apic_status_t g_apic;
static ioapic_instance_t g_ioapics[APIC_MAX_IOAPICS];

/* IRQ → GSI mapping table. By default IRQ_N == GSI_N unless overridden.
   We store the override source IRQ and the remapped GSI + flags. */
#define APIC_MAX_IRQ 32u

typedef struct {
    u8   present;
    u8   irq_src;
    u32  gsi;
    u16  flags;  /* low-active, level-triggered flags from MADT ISO */
} irq_override_t;

static irq_override_t g_irq_map[APIC_MAX_IRQ];

/* Calibration helper */
static volatile u32 g_calib_ticks = 0;
static u8 g_timer_vector = 0x40;  /* Default LAPIC timer vector (IRQ 0 = vec 0x40) */

/* ======================== Forward Declarations ======================== */
static void ioapic_parse_madt(void);
static int  ioapic_set_redir(u32 ioapic_idx, u32 index, u32 low, u32 high);
static void ioapic_get_redir(u32 ioapic_idx, u32 index, u32* low, u32* high);
static int  ioapic_find_for_irq(u8 irq, u32* out_gsi, u32* out_ioapic_idx, u32* out_pin);
static void apic_calibration_isr(registers_t* regs);

/* ======================== LAPIC Primitives ======================== */
u32 apic_lapic_read(u32 reg) {
    if (!g_apic.lapic_addr) return 0;
    volatile u32* p = mmio_map((u64)g_apic.lapic_addr + reg);
    return p ? *p : 0;
}

void apic_lapic_write(u32 reg, u32 val) {
    if (!g_apic.lapic_addr) return;
    volatile u32* p = mmio_map((u64)g_apic.lapic_addr + reg);
    if (!p) return;
    *p = val;
    /* Ensure the write is flushed by reading back */
    (void)*p;
}

u32 apic_lapic_id(void) {
    return (apic_lapic_read(LAPIC_ID) >> 24u) & 0xFFu;
}

void apic_lapic_eoi(void) {
    apic_lapic_write(LAPIC_EOI, 0u);
}

void apic_lapic_enable(void) {
    u32 svr = apic_lapic_read(LAPIC_SVR);
    svr |= LAPIC_SVR_ENABLE;
    svr &= ~LAPIC_SVR_FOCUS;  /* Disable focus processor checking */
    /* Set spurious vector to 0xFF */
    svr = (svr & ~0xFFu) | 0xFFu;
    apic_lapic_write(LAPIC_SVR, svr);
    g_apic.lapic_enabled = 1u;
}

void apic_lapic_disable(void) {
    u32 svr = apic_lapic_read(LAPIC_SVR);
    svr &= ~LAPIC_SVR_ENABLE;
    apic_lapic_write(LAPIC_SVR, svr);
    g_apic.lapic_enabled = 0u;
}

/* ======================== I/O APIC Primitives ======================== */
static u32 ioapic_mmio_read(u32 base, u32 reg) {
    volatile u32* io = mmio_map((u64)base);
    if (!io) return 0;
    io[0] = reg;
    return io[IOAPIC_IOWIN / 4u];
}

static void ioapic_mmio_write(u32 base, u32 reg, u32 val) {
    volatile u32* io = mmio_map((u64)base);
    if (!io) return;
    io[0] = reg;
    io[IOAPIC_IOWIN / 4u] = val;
}

u32 apic_ioapic_read(u32 idx, u32 reg) {
    if (idx >= g_apic.ioapic_count) return 0;
    return ioapic_mmio_read(g_ioapics[idx].base_addr, reg);
}

void apic_ioapic_write(u32 idx, u32 reg, u32 val) {
    if (idx >= g_apic.ioapic_count) return;
    ioapic_mmio_write(g_ioapics[idx].base_addr, reg, val);
}

int apic_ioapic_count(void) {
    return (int)g_apic.ioapic_count;
}

int apic_ioapic_info(u32 idx, ioapic_instance_t* out) {
    if (idx >= g_apic.ioapic_count || !out) return -1;
    *out = g_ioapics[idx];
    return 0;
}

/* ======================== MADT Parsing for I/O APIC ======================== */
static void ioapic_parse_madt(void) {
    hw_topology_status_t hw;
    hw_topology_status(&hw);

    if (!hw.madt_found) return;

    /* Get the MADT virtual address from hw_topology, which already did the
       proper checksum-verified ACPI table walk and mapped the pages through
       the HHDM. No need to re-scan or map. */
    const u8* madt = (const u8*)hw_topology_madt_virt();
    if (!madt) return;

    /* Parse MADT entries */
    u32 madt_len = *(const volatile u32*)(madt + 4u);

    /* LAPIC address is at offset 0x24 in MADT (after header) */
    u32 lapic_addr_from_madt = *(const volatile u32*)(madt + 0x24u);
    if (lapic_addr_from_madt && !g_apic.lapic_addr) {
        g_apic.lapic_addr = lapic_addr_from_madt;
    }

    /* Parse entries starting at offset 0x2C (MADT header = 44 bytes) */
    u32 offset = 0x2Cu;
    while (offset + 2u <= madt_len) {
        u8 type = *(const volatile u8*)(madt + offset);
        u8 len  = *(const volatile u8*)(madt + offset + 1u);

        if (len < 2u || offset + len > madt_len) break;

        if (type == MADT_TYPE_IOAPIC && len >= 12u) {
            /* I/O APIC entry format:
               byte 2: I/O APIC ID
               bytes 4-7: I/O APIC MMIO base address
               bytes 8-11: Global System Interrupt base */
            u32 ioapic_id   = *(const volatile u32*)(madt + offset + 2u) & 0xFFu;
            u32 ioapic_addr = *(const volatile u32*)(madt + offset + 4u);
            u32 gsi_base    = *(const volatile u32*)(madt + offset + 8u);

            if (g_apic.ioapic_count < APIC_MAX_IOAPICS && ioapic_addr) {
                /* Probe the I/O APIC version register to get max redirections */
                u32 ver = ioapic_mmio_read(ioapic_addr, IOAPIC_VER);
                u32 max_redirs = ((ver >> 16) & 0xFFu) + 1u;

                g_ioapics[g_apic.ioapic_count].base_addr  = ioapic_addr;
                g_ioapics[g_apic.ioapic_count].gsi_base   = gsi_base;
                g_ioapics[g_apic.ioapic_count].max_redirs = max_redirs;
                g_ioapics[g_apic.ioapic_count].id         = ioapic_id;
                g_apic.ioapic_count++;
            }
        }
        else if (type == MADT_TYPE_ISO && len >= 8u) {
            /* Interrupt Source Override:
               byte 2-3: IRQ source (bus IRQ)
               byte 4-5: Global System Interrupt
               byte 6-7: Flags (bit 0=trigger mode, bit 1=polarity) */
            u8  irq_src  = *(const volatile u16*)(madt + offset + 2u) & 0xFFu;
            u32 gsi      = *(const volatile u32*)(madt + offset + 4u) & 0xFFFFu;
            u16 flags    = *(const volatile u16*)(madt + offset + 6u);

            if (irq_src < APIC_MAX_IRQ) {
                g_irq_map[irq_src].present = 1u;
                g_irq_map[irq_src].irq_src = irq_src;
                g_irq_map[irq_src].gsi     = gsi;
                g_irq_map[irq_src].flags   = flags;
            }
        }

        offset += len;
    }
}

/* ======================== I/O APIC Redirection ======================== */
static int ioapic_find_for_irq(u8 irq, u32* out_gsi, u32* out_ioapic_idx, u32* out_pin) {
    u32 gsi;
    
    /* Check for override first */
    if (irq < APIC_MAX_IRQ && g_irq_map[irq].present) {
        gsi = g_irq_map[irq].gsi;
    } else {
        gsi = irq;  /* Default: IRQ N → GSI N */
    }

    /* Find which I/O APIC handles this GSI */
    for (u32 i = 0; i < g_apic.ioapic_count; i++) {
        u32 base = g_ioapics[i].gsi_base;
        u32 end  = base + g_ioapics[i].max_redirs;
        if (gsi >= base && gsi < end) {
            if (out_gsi)       *out_gsi = gsi;
            if (out_ioapic_idx) *out_ioapic_idx = i;
            if (out_pin)        *out_pin = gsi - base;
            return 0;
        }
    }

    return -1;  /* Not found */
}

static int ioapic_set_redir(u32 ioapic_idx, u32 index, u32 low, u32 high) {
    if (ioapic_idx >= g_apic.ioapic_count) return -1;
    if (index >= g_ioapics[ioapic_idx].max_redirs) return -1;
    u32 base = g_ioapics[ioapic_idx].base_addr;
    ioapic_mmio_write(base, IOAPIC_REDIR_BASE + index * 2u, low);
    ioapic_mmio_write(base, IOAPIC_REDIR_BASE + index * 2u + 1u, high);
    return 0;
}

static void ioapic_get_redir(u32 ioapic_idx, u32 index, u32* low, u32* high) {
    if (ioapic_idx >= g_apic.ioapic_count) return;
    if (index >= g_ioapics[ioapic_idx].max_redirs) return;
    u32 base = g_ioapics[ioapic_idx].base_addr;
    if (low)  *low  = ioapic_mmio_read(base, IOAPIC_REDIR_BASE + index * 2u);
    if (high) *high = ioapic_mmio_read(base, IOAPIC_REDIR_BASE + index * 2u + 1u);
}

/* ======================== Public IRQ Routing API ======================== */
void apic_redirect_irq(u8 irq, u8 vector, u32 dest_apic_id, u8 trigger_mode) {
    u32 gsi, ioapic_idx, pin;
    if (ioapic_find_for_irq(irq, &gsi, &ioapic_idx, &pin) != 0) return;

    u16 flags = 0;
    if (irq < APIC_MAX_IRQ && g_irq_map[irq].present) {
        flags = g_irq_map[irq].flags;
    }

    u32 low = vector;
    /* Delivery mode: Fixed (0) */
    /* Destination mode: Physical (0) */
    /* Polarity: from flags (bit 1 = polarity, bit 0 = trigger override) */
    if (flags & 0x2u) {
        low |= IOAPIC_REDIR_POLARITY;  /* Low-active */
    }
    if (flags & 0x8u) {
        low |= IOAPIC_REDIR_TRIGGER;   /* Level-triggered */
    } else if (trigger_mode) {
        low |= IOAPIC_REDIR_TRIGGER;
    }
    /* Start masked - we'll unmask when ready */
    low |= IOAPIC_REDIR_MASK;

    u32 high = (dest_apic_id & 0xFFu) << 24u;

    ioapic_set_redir(ioapic_idx, pin, low, high);
}

void apic_mask_irq(u8 irq) {
    u32 gsi, ioapic_idx, pin;
    if (ioapic_find_for_irq(irq, &gsi, &ioapic_idx, &pin) != 0) return;

    u32 low, high;
    ioapic_get_redir(ioapic_idx, pin, &low, &high);
    low |= IOAPIC_REDIR_MASK;
    ioapic_set_redir(ioapic_idx, pin, low, high);
}

void apic_unmask_irq(u8 irq) {
    u32 gsi, ioapic_idx, pin;
    if (ioapic_find_for_irq(irq, &gsi, &ioapic_idx, &pin) != 0) return;

    u32 low, high;
    ioapic_get_redir(ioapic_idx, pin, &low, &high);
    low &= ~IOAPIC_REDIR_MASK;
    ioapic_set_redir(ioapic_idx, pin, low, high);
}

void apic_mask_all(void) {
    for (u32 i = 0; i < g_apic.ioapic_count; i++) {
        for (u32 j = 0; j < g_ioapics[i].max_redirs; j++) {
            u32 low, high;
            ioapic_get_redir(i, j, &low, &high);
            low |= IOAPIC_REDIR_MASK;
            ioapic_set_redir(i, j, low, high);
        }
    }
}

/* ======================== Inter-Processor Interrupts (IPI) ======================== */
void apic_send_ipi(u32 apic_id, u32 icr_low) {
    apic_lapic_write(LAPIC_ICR_HIGH, (apic_id & 0xFFu) << 24u);
    apic_lapic_write(LAPIC_ICR_LOW, icr_low);
    /* Wait for delivery */
    for (u32 i = 0; i < 1000u; i++) {
        if ((apic_lapic_read(LAPIC_ICR_LOW) & LAPIC_ICR_DELIVERY) == 0u) return;
        __asm__ __volatile__("pause");
    }
}

void apic_send_init_ipi(u32 apic_id) {
    apic_send_ipi(apic_id, LAPIC_ICR_INIT | 0x8000u);  /* Assert INIT */
    /* Wait 10ms */
    for (volatile u32 i = 0; i < 100000u; i++) __asm__ __volatile__("pause");
    apic_send_ipi(apic_id, LAPIC_ICR_INIT);  /* Deassert */
    for (volatile u32 i = 0; i < 200000u; i++) __asm__ __volatile__("pause");
}

void apic_send_startup_ipi(u32 apic_id, u8 vector) {
    apic_send_ipi(apic_id, LAPIC_ICR_STARTUP | vector);
    for (volatile u32 i = 0; i < 10000u; i++) __asm__ __volatile__("pause");
    /* Send second startup IPI as per Intel manual recommendation */
    apic_send_ipi(apic_id, LAPIC_ICR_STARTUP | vector);
}

/* ======================== LAPIC Timer ======================== */
static u32 apic_timer_estimate_freq(void) {
    /* Calibrate APIC timer frequency against the PIT (8253/8254).
       
       Method:
       1. Program PIT counter 0 with a known reload value (0xFFFF).
          PIT runs at 1.193181818 MHz → period = 0xFFFF ticks ≈ 54.9ms.
       2. Start APIC timer in one-shot mode with max count.
       3. Poll PIT status until OUT=1 (count reached 0) with timeout.
       4. Read remaining APIC timer count.
       5. frequency_apic = elapsed_apic_ticks * 1193180 / pit_reload
    */
    
    u32 pit_reload = 0xFFFFu;

    /* Set PIT counter 0: mode 0 (interrupt on terminal count), 16-bit */
    outb(0x43, 0x30u);
    outb(0x40, pit_reload & 0xFFu);
    outb(0x40, (pit_reload >> 8) & 0xFFu);

    /* Set APIC timer divider to 1 for maximum precision */
    apic_lapic_write(LAPIC_TIMER_DIV, LAPIC_TIMER_DIV_1);

    /* Start APIC timer with max count */
    apic_lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFFu);

    /* Poll PIT status until OUT=1, with timeout to avoid hangs.
       PIT mode 0 with 0xFFFF reload at 1.19318 MHz takes ~54.9ms.
       At ~1GHz CPU that's ~55M cycles, so 5M polling iterations is safe. */
    u32 timeout = 5000000u;
    u8 pit_done = 0u;
    for (volatile u32 i = 0; i < timeout; i++) {
        /* Readback status of counter 0 only: 11 0 1 1 0 1 0 = 0xCA.
           0xE2 latches counters 1+2 instead, so inb(0x40) returns a raw
           count-low byte whose bit 7 flips at 0x7FFF — ending the poll
           ~3.4x too early and mis-calibrating the LAPIC timer. */
        outb(0x43, 0xCAu);
        u8 status = inb(0x40);
        if (status & 0x80u) {
            pit_done = 1u;
            break;
        }
        __asm__ __volatile__("pause");
    }

    /* Read APIC timer remaining count */
    u32 remaining = apic_lapic_read(LAPIC_TIMER_CUR);
    
    /* Stop APIC timer */
    apic_lapic_write(LAPIC_TIMER_INIT, 0u);
    
    /* Handle timeout or zero-elapsed edge case */
    if (!pit_done) {
        /* PIT didn't reach 0 — use a conservative fallback estimate.
           Most CPUs have APIC timers in the 1-100 MHz range.
           We'll assume a typical ~10 MHz and let the caller retry. */
        return 10000000u;  /* 10 MHz fallback */
    }
    
    u32 elapsed = 0xFFFFFFFFu - remaining;
    if (elapsed == 0u) {
        return 10000000u;  /* fallback */
    }

    /* frequency_apic = elapsed_apic * PIT_freq / pit_reload
       PIT_freq = 1193181.818... ≈ 1193180 Hz
       = elapsed * 1193180 / 0xFFFF */
    /* Use 64-bit math: elapsed * 1193180 overflows u32 when the APIC timer
       runs fast (elapsed near 0xFFFFFFFF), which produced a bogus tiny
       init_count and an IRQ0 storm during boot. */
    u64 freq64 = ((u64)elapsed * 1193180ull) / pit_reload;
    if (freq64 > 100000000ull) freq64 = 100000000ull;  /* clamp 100 MHz */
    if (freq64 < 1000000ull) freq64 = 1000000ull;      /* floor 1 MHz */
    return (u32)freq64;
}

static void apic_calibration_isr(registers_t* regs) {
    (void)regs;
    g_calib_ticks++;
    apic_lapic_eoi();
}

u32 apic_timer_calibrate(void) {
    u32 freq_hz = apic_timer_estimate_freq();
    g_apic.lapic_freq_khz = freq_hz / 1000u;
    if (g_apic.lapic_freq_khz == 0u) g_apic.lapic_freq_khz = 1000u;  /* fallback */
    return freq_hz;
}

int apic_timer_init(u32 frequency_hz, u8 vector) {
    if (!g_apic.lapic_addr || !g_apic.lapic_enabled) return -1;

    g_timer_vector = vector;

    /* Calibrate to get the APIC timer frequency */
    if (g_apic.lapic_freq_khz == 0u) {
        apic_timer_calibrate();
    }

    /* Set divider to 1 */
    apic_lapic_write(LAPIC_TIMER_DIV, LAPIC_TIMER_DIV_1);

    /* Calculate initial count = freq_hz / frequency_hz */
    u32 freq_hz = g_apic.lapic_freq_khz * 1000u;
    u32 init_count = freq_hz / frequency_hz;
    if (init_count == 0u) init_count = 1u;

    /* Program LVT Timer register: periodic mode, unmasked, vector */
    u32 lvt = vector | LAPIC_TIMER_PERIODIC;
    apic_lapic_write(LAPIC_TIMER, lvt);

    /* Set initial count */
    apic_lapic_write(LAPIC_TIMER_INIT, init_count);

    g_apic.timer_mode = 1u;  /* LAPIC timer active */
    return 0;
}

int apic_timer_oneshot(u32 count) {
    if (!g_apic.lapic_addr) return -1;

    u32 lvt = g_timer_vector | LAPIC_TIMER_ONESHOT;
    apic_lapic_write(LAPIC_TIMER, lvt);
    apic_lapic_write(LAPIC_TIMER_INIT, count);
    return 0;
}

int apic_timer_stop(void) {
    if (!g_apic.lapic_addr) return -1;
    apic_lapic_write(LAPIC_TIMER, LAPIC_TIMER_MASK);  /* Mask timer */
    apic_lapic_write(LAPIC_TIMER_INIT, 0u);
    g_apic.timer_mode = 0u;
    return 0;
}

u32 apic_timer_read_count(void) {
    return apic_lapic_read(LAPIC_TIMER_CUR);
}

u32 apic_timer_freq_khz(void) {
    return g_apic.lapic_freq_khz;
}

int apic_timer_tsc_deadline(u8 vector) {
    /* Check CPU support for TSC deadline mode */
    u32 a, b, c, d;
    __asm__ __volatile__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    if (!(c & (1u << 24))) return -1;  /* No TSC deadline support */

    u32 lvt = vector | LAPIC_TIMER_TSC_DEADLINE;
    apic_lapic_write(LAPIC_TIMER, lvt);
    return 0;
}

/* ======================== Full APIC Initialization ======================== */
void apic_init(void) {
    serial_writeln("[APIC] init start");

    /* Zero out state */
    memset(&g_apic, 0, sizeof(g_apic));
    memset(g_ioapics, 0, sizeof(g_ioapics));
    memset(g_irq_map, 0, sizeof(g_irq_map));

    /* Get LAPIC address from hw_topology which already parsed the MADT */
    {
        hw_topology_status_t hw;
        hw_topology_status(&hw);
        if (hw.madt_found && hw.lapic_addr) {
            g_apic.lapic_addr = hw.lapic_addr;
        }
    }

    if (!g_apic.lapic_addr) {
        serial_writeln("[APIC] no LAPIC address found, APIC unavailable");
        g_apic.available = 0u;
        return;
    }

    /* Enable the LAPIC */
    apic_lapic_enable();

    /* Get BSP APIC ID */
    g_apic.bsp_apic_id = apic_lapic_id();

    serial_write("[APIC] BSP APIC ID=");
    serial_write_dec(g_apic.bsp_apic_id);
    serial_writeln("");

    /* Parse MADT for I/O APICs and interrupt overrides */
    ioapic_parse_madt();

    serial_write("[APIC] I/O APIC count=");
    serial_write_dec(g_apic.ioapic_count);
    serial_writeln("");

    /* If we have I/O APICs, route IRQs */
    if (g_apic.ioapic_count > 0u) {
        for (u32 i = 0; i < g_apic.ioapic_count; i++) {
            serial_write("[APIC] IOAPIC#");
            serial_write_dec(i);
            serial_write(" addr=0x");
            serial_write_hex(g_ioapics[i].base_addr);
            serial_write(" gsi_base=");
            serial_write_dec(g_ioapics[i].gsi_base);
            serial_write(" entries=");
            serial_write_dec(g_ioapics[i].max_redirs);
            serial_writeln("");
        }

        /* Dump interrupt overrides */
        for (u32 i = 0; i < APIC_MAX_IRQ; i++) {
            if (g_irq_map[i].present) {
                serial_write("[APIC] IRQ override: bus_irq=");
                serial_write_dec(g_irq_map[i].irq_src);
                serial_write(" gsi=");
                serial_write_dec(g_irq_map[i].gsi);
                serial_writeln("");
            }
        }

        /* Route standard IRQs 0-15 through I/O APIC to BSP */
        for (u8 irq = 0; irq < 16u; irq++) {
            u8 vector = 0x20u + irq;  /* Same vector mapping as PIC remap */
            apic_redirect_irq(irq, vector, g_apic.bsp_apic_id, 0u);
        }

        /* Route IRQ 2 (cascade) - typically handled specially, mask it */
        apic_mask_irq(2);

        serial_writeln("[APIC] IRQ routing configured");
    }

    g_apic.available = 1u;
    serial_writeln("[APIC] init complete");
}

int apic_is_available(void) {
    return g_apic.available ? 1 : 0;
}

void apic_status(apic_status_t* out) {
    if (out) *out = g_apic;
}

void apic_dump_status(void) {
    apic_status_t st;
    apic_status(&st);

    serial_write("[APIC-DUMP] available=");
    serial_write_dec(st.available);
    serial_write(" lapic_enabled=");
    serial_write_dec(st.lapic_enabled);
    serial_write(" lapic_addr=0x");
    serial_write_hex(st.lapic_addr);
    serial_write(" bsp_id=");
    serial_write_dec(st.bsp_apic_id);
    serial_write(" timer_mode=");
    serial_write_dec(st.timer_mode);
    serial_write(" freq_khz=");
    serial_write_dec(st.lapic_freq_khz);
    serial_write(" ioapics=");
    serial_write_dec(st.ioapic_count);
    serial_writeln("");
}
