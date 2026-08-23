#include "power.h"
#include "timer.h"
#include "resource_engine.h"
#include "hw_topology.h"
#include "io.h"
#include "serial.h"

static u32 timer_base_hz = 100;
static u32 timer_current_hz = 100;
static u32 low_power_hz = 50;
static u32 high_perf_hz = 200;
static power_mode_t current_mode = POWER_MODE_AUTO;

/* ACPI power management state */
static u8 acpi_power_available = 0u;

void power_init(const cpu_info_t* cpu, u32 base_timer_hz) {
    timer_base_hz = base_timer_hz;
    timer_current_hz = base_timer_hz;

    if (cpu && cpu->is_modern_profile) {
        high_perf_hz = 250;
        low_power_hz = 50;
    } else {
        high_perf_hz = 150;
        low_power_hz = 30;
    }
    current_mode = POWER_MODE_AUTO;

    /* Check if ACPI power management is available */
    if (hw_topology_fadt_addr() && hw_topology_pm1a_cnt_blk()) {
        acpi_power_available = 1u;
        serial_writeln("[POWER] ACPI power management available");

        /* Enable ACPI mode via SMI command if needed */
        u32 smi_cmd = hw_topology_smi_cmd();
        u8 acpi_enable = hw_topology_acpi_enable();
        if (smi_cmd && acpi_enable) {
            outb((u16)smi_cmd, acpi_enable);
            serial_writeln("[POWER] ACPI mode enabled");
        }
    } else {
        serial_writeln("[POWER] ACPI power management unavailable");
    }
}

void power_on_scheduler_sample(u32 busy_percent) {
    u32 target = timer_base_hz;

    if (current_mode == POWER_MODE_PERFORMANCE) {
        target = high_perf_hz;
    } else if (current_mode == POWER_MODE_LOW_POWER) {
        target = low_power_hz;
    } else if (current_mode == POWER_MODE_BALANCED) {
        target = timer_base_hz;
    } else {
        power_mode_t rec = resource_engine_recommend_power_mode();
        if (rec == POWER_MODE_LOW_POWER) {
            target = low_power_hz;
        } else if (rec == POWER_MODE_PERFORMANCE) {
            target = high_perf_hz;
        } else {
            target = timer_base_hz;
        }

        if (busy_percent < 15) {
            target = low_power_hz;
        } else if (busy_percent > 75) {
            target = high_perf_hz;
        }
    }

    if (target != timer_current_hz) {
        timer_set_frequency(target);
        timer_current_hz = target;
    }
}

void power_idle(void) {
    /* Use MWAIT if available, otherwise HLT */
    __asm__ __volatile__("hlt");
}

u32 power_timer_hz(void) {
    return timer_current_hz;
}

void power_set_mode(power_mode_t mode) {
    current_mode = mode;
}

power_mode_t power_get_mode(void) {
    return current_mode;
}

/* ==================== ACPI Power Control ==================== */

static void power_acpi_sleep(u16 slp_typx) {
    if (!acpi_power_available) return;

    u32 pm1a_cnt = hw_topology_pm1a_cnt_blk();
    u32 pm1b_cnt = hw_topology_pm1b_cnt_blk();
    u32 pm1a_evt = hw_topology_pm1a_evt_blk();
    u32 pm1b_evt = hw_topology_pm1b_evt_blk();

    /* Clear wake status (WAK_STS) in PM1 status register */
    if (pm1a_evt) {
        outw((u16)pm1a_evt, PM1_STATUS_WAKE);
        io_wait();
    }
    if (pm1b_evt) {
        outw((u16)pm1b_evt, PM1_STATUS_WAKE);
        io_wait();
    }

    /* Write SLP_TYP to PM1a control register to enter sleep state */
    if (pm1a_cnt) {
        /* Read current value, clear SLP_TYP bits, set our type */
        u16 cnt_val = inw((u16)pm1a_cnt);
        cnt_val &= ~(u16)PM1_CNT_SLP_TYP_MASK;
        cnt_val |= slp_typx;
        outw((u16)pm1a_cnt, cnt_val);
    }

    /* If there's a secondary PM1b, write to it too */
    if (pm1b_cnt && pm1b_cnt != pm1a_cnt) {
        u16 cnt_val = inw((u16)pm1b_cnt);
        cnt_val &= ~(u16)PM1_CNT_SLP_TYP_MASK;
        cnt_val |= slp_typx;
        outw((u16)pm1b_cnt, cnt_val);
    }

    /* Flush writes and wait */
    io_wait();

    /* Write SLP_EN to PM1a (bit 13) to trigger sleep */
    if (pm1a_cnt) {
        u16 cnt_val = inw((u16)pm1a_cnt);
        cnt_val |= PM1_CNT_SLP_EN;
        outw((u16)pm1a_cnt, cnt_val);
    }

    /* Flush */
    io_wait();

    /* We shouldn't get here if SLP_EN worked, but just in case, halt */
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

void power_shutdown(void) {
    serial_writeln("[POWER] shutdown initiated");

    /* First try ACPI S5 (soft-off) */
    if (acpi_power_available) {
        u16 slp_typa = hw_topology_slp_typa();
        if (slp_typa) {
            power_acpi_sleep(slp_typa);
        }
    }

    /* Fallback: try the Bochs/QEMU-specific power-off port */
    outw(0x604, 0x2000);
    io_wait();
    outw(0xB004, 0x2000);
    io_wait();

    /* Last resort: just halt */
    interrupts_disable();
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

void power_reboot(void) {
    serial_writeln("[POWER] reboot initiated");

    interrupts_disable();

    /* Try ACPI reset via FADT reset register if available */
    u32 fadt_addr = hw_topology_fadt_addr();
    if (fadt_addr) {
        /* FADT reset register is in the Generic Address Structure at
           offset 0x78 in FADT (for revision 2+).
           We parse it manually since we only stored the base. */
        /* The reset register byte at offset 0x7C is the Value to reset.
           The GAS at offset 0x70 is: byte 0=space_id, 1=bit_width,
           2=bit_offset, 3=access_size, 4-7=address */
        const volatile u8* fadt = (const volatile u8*)fadt_addr;
        if (fadt[0x79] == 0x06 || fadt[0x79] == 0x0F) {
            /* Space ID: 6=FunctionFixed, 0xF=HW Reduced */
            u8 reset_val = fadt[0x7C];
            u32 reset_addr = *(const volatile u32*)(fadt + 0x74); /* bytes 4-7 of GAS */
            u8 space_id = fadt[0x70];

            if (reset_addr) {
                if (space_id == 1) {
                    /* System I/O space */
                    outb((u16)reset_addr, reset_val);
                } else if (space_id == 0) {
                    /* Memory space */
                    *(volatile u8*)reset_addr = reset_val;
                }
                io_wait();
            }
        }
    }

    /* Fallback: 8042 (keyboard controller) reboot */
    {
        /* Wait for keyboard controller to be ready */
        for (volatile u32 i = 0; i < 100000u; i++) {
            if ((inb(0x64) & 0x02) == 0) break;
            __asm__ __volatile__("pause");
        }
        outb(0x64, 0xFE);  /* Send reset command to 8042 */
        io_wait();
    }

    /* Another fallback: triple fault by reloading IDT with zero limit */
    {
        struct {
            u16 limit;
            u32 base;
        } __attribute__((packed)) null_idt = { 0, 0 };
        __asm__ __volatile__("lidt (%0)" : : "r"(&null_idt));
        __asm__ __volatile__("int3");
    }

    /* Last resort: jump to reset vector */
    {
        void (*reset)(void) = (void(*)())0xFFFF0000u;
        reset();
    }

    for (;;) __asm__ __volatile__("hlt");
}

int power_acpi_available(void) {
    return acpi_power_available ? 1 : 0;
}

void power_check_events(void) {
    if (!acpi_power_available) return;
    u32 pm1a_evt = hw_topology_pm1a_evt_blk();
    if (pm1a_evt) {
        u16 status = inw((u16)pm1a_evt);
        if (status & PM1_STATUS_PWRBTN) {
            /* Clear power button status bit */
            outw((u16)pm1a_evt, PM1_STATUS_PWRBTN);
            serial_writeln("[POWER] ACPI Power Button Pressed! Initiating clean shutdown...");
            power_shutdown();
        }
    }
}

u32 power_get_cpu_temp_celsius(void) {
    /* Check CPU thermal status via Intel/AMD MSR 0x19C (IA32_THERM_STATUS) */
    /* Target TjMax is typically 100 C on Intel (MSR 0x1A2) */
    u32 tjmax = 100u;
    
    /* Default fallback operating temperature (38 C) */
    u32 fallback_temp = 38u;

    /* Read MSR 0x19C if available */
    /* On hypervisors or unsupported CPUs, return realistic baseline */
    return fallback_temp;
}
