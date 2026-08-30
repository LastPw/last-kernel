#include "laptop_platform.h"
#include "string.h"
#include "spinlock.h"
#include "timer.h"
#include "screen.h"

/* ==============================================================================
 * Smart Battery Subsystem (SBS 1.1) & Power Supply Manager
 * ============================================================================== */

static battery_info_t s_battery;
static spinlock_t s_bat_lock;

void battery_sbs_init(void) {
    spinlock_init(&s_bat_lock);
    u32 flags = spinlock_lock_irqsave(&s_bat_lock);

    s_battery.present = 1;
    s_battery.ac_online = 1;
    s_battery.status_flags = BATTERY_STATUS_CHARGING;
    s_battery.state_of_charge = 85; /* 85% */
    s_battery.voltage_mv = 12600;   /* 12.6V 3S Li-Po */
    s_battery.current_ma = 1500;    /* +1500mA charging */
    s_battery.remaining_capacity_mah = 4250;
    s_battery.full_charge_capacity_mah = 5000;
    s_battery.design_capacity_mah = 5200;
    s_battery.cycle_count = 48;
    s_battery.minutes_remaining = 30; /* 30 mins to full */
    s_battery.temperature_celsius_x10 = 295; /* 29.5 C */
    s_battery.health_percentage = (u8)((s_battery.full_charge_capacity_mah * 100u) / s_battery.design_capacity_mah);

    str_ncpy(s_battery.technology, "Li-Poly", sizeof(s_battery.technology));
    str_ncpy(s_battery.vendor, "Axior-Power", sizeof(s_battery.vendor));
    str_ncpy(s_battery.model, "BAT-99WH-PRO", sizeof(s_battery.model));

    spinlock_unlock_irqrestore(&s_bat_lock, flags);
}

void battery_sbs_poll(void) {
    u32 flags = spinlock_lock_irqsave(&s_bat_lock);

    /* Read ACPI EC Battery registers */
    u8 raw_soc = 0;
    if (acpi_ec_read(0x01, &raw_soc) == 0 && raw_soc > 0 && raw_soc <= 100) {
        s_battery.state_of_charge = raw_soc;
    }

    u8 raw_ac = 0;
    if (acpi_ec_read(0x00, &raw_ac) == 0) {
        s_battery.ac_online = raw_ac ? 1 : 0;
        if (s_battery.ac_online) {
            s_battery.status_flags = (s_battery.state_of_charge >= 99) ? BATTERY_STATUS_FULL : BATTERY_STATUS_CHARGING;
            s_battery.current_ma = 1200;
            if (s_battery.state_of_charge < 100) {
                u32 needed_mah = s_battery.full_charge_capacity_mah - s_battery.remaining_capacity_mah;
                s_battery.minutes_remaining = (u16)((needed_mah * 60u) / (u32)s_battery.current_ma);
            } else {
                s_battery.minutes_remaining = 0;
            }
        } else {
            s_battery.status_flags = (s_battery.state_of_charge <= 10) ? BATTERY_STATUS_CRITICAL : BATTERY_STATUS_DISCHARGING;
            s_battery.current_ma = -950; /* 950mA discharge */
            if (s_battery.current_ma < 0) {
                s_battery.minutes_remaining = (u16)((s_battery.remaining_capacity_mah * 60u) / (u32)(-s_battery.current_ma));
            }
        }
    }

    s_battery.remaining_capacity_mah = (s_battery.full_charge_capacity_mah * (u32)s_battery.state_of_charge) / 100u;

    spinlock_unlock_irqrestore(&s_bat_lock, flags);
}

int battery_sbs_get_info(battery_info_t* out) {
    if (!out) return -1;
    u32 flags = spinlock_lock_irqsave(&s_bat_lock);
    *out = s_battery;
    spinlock_unlock_irqrestore(&s_bat_lock, flags);
    return 0;
}

u8 battery_sbs_is_charging(void) {
    u32 flags = spinlock_lock_irqsave(&s_bat_lock);
    u8 chg = (s_battery.status_flags & BATTERY_STATUS_CHARGING) ? 1 : 0;
    spinlock_unlock_irqrestore(&s_bat_lock, flags);
    return chg;
}

u8 battery_sbs_get_percentage(void) {
    u32 flags = spinlock_lock_irqsave(&s_bat_lock);
    u8 soc = s_battery.state_of_charge;
    spinlock_unlock_irqrestore(&s_bat_lock, flags);
    return soc;
}
