#ifndef POWER_H
#define POWER_H

#include "types.h"
#include "cpu_detect.h"
#include "interrupts.h"

typedef enum {
    POWER_MODE_PERFORMANCE = 0,
    POWER_MODE_BALANCED = 1,
    POWER_MODE_LOW_POWER = 2,
    POWER_MODE_AUTO = 3
} power_mode_t;

/* PM1 status register bits (for ACPI power management) */
#define PM1_STATUS_WAKE           0x8000u
#define PM1_STATUS_PWRBTN         0x0100u
#define PM1_STATUS_SLPBTN         0x0200u

/* PM1 control register bits */
#define PM1_CNT_SLP_EN            0x2000u
#define PM1_CNT_SLP_TYP_SHIFT     10u
#define PM1_CNT_SLP_TYP_MASK      0x1C00u

void power_init(const cpu_info_t* cpu, u32 base_timer_hz);
void power_on_scheduler_sample(u32 busy_percent);
void power_idle(void);
u32 power_timer_hz(void);
void power_set_mode(power_mode_t mode);
power_mode_t power_get_mode(void);

/* ACPI power control */
void  power_shutdown(void);
void  power_reboot(void);
int   power_acpi_available(void);
void  power_check_events(void);
u32   power_get_cpu_temp_celsius(void);

#endif
