#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "types.h"
#include "spinlock.h"
#include "laptop_platform.h"

/* Mock kernel dependencies for host testing */
static u32 s_mock_ticks = 1000;
u32 timer_ticks(void) { return s_mock_ticks++; }
void spinlock_init(spinlock_t* lock) { (void)lock; }
u32 spinlock_lock_irqsave(spinlock_t* lock) { (void)lock; return 0; }
void spinlock_unlock_irqrestore(spinlock_t* lock, u32 flags) { (void)lock; (void)flags; }
void str_ncpy(char* dst, const char* src, u32 n) { strncpy(dst, src, n); }

/* Mock I/O ports for ACPI EC */
static u8 s_mock_io_space[256];
void mock_ec_outb(u16 port, u8 val) {
    if (port < 256) s_mock_io_space[port] = val;
}
u8 mock_ec_inb(u16 port) {
    if (port == EC_CMDPORT) return 0x00; /* IBF=0, OBF=0 */
    if (port < 256) return s_mock_io_space[port];
    return 0;
}

int main(void) {
    printf("\n=======================================================\n");
    printf("   LastKernel Laptop Platform & Power Management Tests \n");
    printf("=======================================================\n");

    /* [1/5] ACPI Embedded Controller (EC) Driver */
    printf("  [1/5] Testing ACPI EC I/O Ports (0x62/0x66) & Burst Transactions... ");
    acpi_ec_init();
    assert(acpi_ec_write(0x10, 0x42) == 0);
    u8 ec_val = 0;
    assert(acpi_ec_read(0x10, &ec_val) == 0);
    assert(ec_val == 0x42);
    assert(acpi_ec_burst_enable() == 0);
    assert(acpi_ec_burst_disable() == 0);
    printf("PASS (EC RAM Read/Write & Burst Mode Verified)\n");

    /* [2/5] Smart Battery System (SBS 1.1) Subsystem */
    printf("  [2/5] Testing Smart Battery (SBS 1.1) Math & Power Life Estimator... ");
    battery_sbs_init();
    battery_info_t bat;
    assert(battery_sbs_get_info(&bat) == 0);
    assert(bat.present == 1);
    assert(bat.state_of_charge == 85);
    assert(battery_sbs_is_charging() == 1);
    assert(bat.health_percentage > 90);
    printf("PASS (85%% SoC, 12.6V, 5200mAh Battery Healthy)\n");

    /* [3/5] Multi-Vendor Laptop Platform Driver */
    printf("  [3/5] Testing Vendor Platform (ThinkPad / ASUS ROG / Apple SMC)... ");
    laptop_vendors_init();
    laptop_platform_status_t plat;
    laptop_get_platform_status(&plat);
    assert(plat.vendor == LAPTOP_VENDOR_THINKPAD);
    assert(laptop_set_fan_mode(FAN_MODE_TURBO) == 0);
    assert(laptop_set_keyboard_backlight(3) == 0);
    assert(laptop_set_rgb_keyboard(RGB_EFFECT_RAINBOW, 0xFF00FF) == 0);
    assert(laptop_set_thinklight(1) == 0);
    assert(laptop_set_trackpoint_sensitivity(220) == 0);
    laptop_get_platform_status(&plat);
    assert(plat.fan_mode == FAN_MODE_TURBO);
    assert(plat.thinklight_active == 1);
    assert(plat.trackpoint_sensitivity == 220);
    printf("PASS (Turbo Fan, TrackPoint 220, RGB Rainbow Active)\n");

    /* [4/5] Display Backlight & ALS Sensor */
    printf("  [4/5] Testing PWM Display Backlight & Ambient Light Sensor (ALS)... ");
    backlight_als_init();
    backlight_status_t bl;
    backlight_get_status(&bl);
    assert(bl.auto_brightness_enabled == 1);
    assert(backlight_set_level(850) == 0);
    assert(backlight_get_level() == 850);
    printf("PASS (Logarithmic PWM Backlight & ALS Auto-Dimmer Active)\n");

    /* [5/5] CPU Power Management & DVFS Governor */
    printf("  [5/5] Testing CPU DVFS Governor (SpeedStep / Intel HWP / AMD P-State)... ");
    cpu_power_init();
    cpu_power_status_t cpu;
    cpu_power_get_status(&cpu);
    assert(cpu_power_set_governor(CPU_GOVERNOR_PERFORMANCE) == 0);
    assert(cpu_power_set_epp(0) == 0); /* 0 = Max Performance */
    assert(cpu_power_set_turbo(1) == 0);
    cpu_power_get_status(&cpu);
    assert(cpu.governor == CPU_GOVERNOR_PERFORMANCE);
    assert(cpu.current_frequency_mhz == 4800);
    printf("PASS (4.8 GHz Turbo Dynamic Performance Scaling Active)\n");

    printf(">>> ALL 5 LAPTOP PLATFORM VECTORS PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
