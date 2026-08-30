#ifndef LAPTOP_PLATFORM_H
#define LAPTOP_PLATFORM_H

#include "types.h"

/* ==============================================================================
 * ACPI Embedded Controller (EC) Definitions
 * ============================================================================== */

#define EC_DATAPORT         0x62
#define EC_CMDPORT          0x66

#define EC_FLAG_OBF         0x01    /* Output Buffer Full */
#define EC_FLAG_IBF         0x02    /* Input Buffer Full */
#define EC_FLAG_CMD         0x08    /* Command (1) / Data (0) */
#define EC_FLAG_BURST       0x10    /* Burst Mode Active */
#define EC_FLAG_SCI_EVT     0x20    /* SCI Event Pending */
#define EC_FLAG_SMI_EVT     0x40    /* SMI Event Pending */

#define EC_CMD_RD_EC        0x80    /* Read byte from EC address space */
#define EC_CMD_WR_EC        0x81    /* Write byte to EC address space */
#define EC_CMD_BE_EC        0x82    /* Burst Enable */
#define EC_CMD_BD_EC        0x83    /* Burst Disable */
#define EC_CMD_QR_EC        0x84    /* Query EC for SCI event code */

/* ==============================================================================
 * Smart Battery System (SBS 1.1) & Power State Definitions
 * ============================================================================== */

#define SBS_REG_MANUFACTURER_ACCESS 0x00
#define SBS_REG_REMAINING_CAP_ALARM 0x01
#define SBS_REG_REMAINING_TIME_ALARM 0x02
#define SBS_REG_BATTERY_MODE        0x03
#define SBS_REG_AT_RATE             0x04
#define SBS_REG_AT_RATE_TIME_TO_FULL 0x05
#define SBS_REG_AT_RATE_TIME_TO_EMPTY 0x06
#define SBS_REG_TEMPERATURE         0x08
#define SBS_REG_VOLTAGE             0x09
#define SBS_REG_CURRENT             0x0A
#define SBS_REG_AVERAGE_CURRENT     0x0B
#define SBS_REG_RELATIVE_SOC        0x0D    /* Relative State of Charge (%) */
#define SBS_REG_ABSOLUTE_SOC        0x0E
#define SBS_REG_REMAINING_CAPACITY  0x0F    /* mAh or 10mWh */
#define SBS_REG_FULL_CHARGE_CAPACITY 0x10
#define SBS_REG_RUN_TIME_TO_EMPTY   0x11    /* minutes */
#define SBS_REG_AVERAGE_TIME_TO_EMPTY 0x12
#define SBS_REG_AVERAGE_TIME_TO_FULL  0x13
#define SBS_REG_CHARGING_CURRENT    0x14
#define SBS_REG_CHARGING_VOLTAGE    0x15
#define SBS_REG_BATTERY_STATUS      0x16
#define SBS_REG_CYCLE_COUNT         0x17
#define SBS_REG_DESIGN_CAPACITY     0x18
#define SBS_REG_DESIGN_VOLTAGE      0x19
#define SBS_REG_DEVICE_CHEMISTRY    0x22
#define SBS_REG_MANUFACTURER_NAME   0x20
#define SBS_REG_DEVICE_NAME         0x21

/* Battery Power Status Flags */
#define BATTERY_STATUS_DISCHARGING  0x01
#define BATTERY_STATUS_CHARGING     0x02
#define BATTERY_STATUS_CRITICAL     0x04
#define BATTERY_STATUS_FULL         0x08
#define BATTERY_STATUS_NOT_PRESENT  0x10

typedef struct {
    u8  present;
    u8  ac_online;
    u8  status_flags;
    u8  state_of_charge;            /* 0 to 100% */
    u32 voltage_mv;                 /* millivolts */
    i32 current_ma;                 /* negative = discharging, positive = charging */
    u32 remaining_capacity_mah;
    u32 full_charge_capacity_mah;
    u32 design_capacity_mah;
    u32 cycle_count;
    u16 minutes_remaining;          /* estimated time to empty or full */
    u16 temperature_celsius_x10;    /* e.g. 352 = 35.2 C */
    u8  health_percentage;          /* FullCharge / DesignCapacity * 100 */
    char technology[16];            /* "Li-Ion", "Li-Poly", "NiMH" */
    char vendor[32];
    char model[32];
} battery_info_t;

/* ==============================================================================
 * Multi-Vendor Laptop Platform Definitions (ThinkPad, ASUS ROG, Dell, MacBook)
 * ============================================================================== */

typedef enum {
    LAPTOP_VENDOR_GENERIC   = 0,
    LAPTOP_VENDOR_THINKPAD  = 1,
    LAPTOP_VENDOR_ASUS_ROG  = 2,
    LAPTOP_VENDOR_DELL      = 3,
    LAPTOP_VENDOR_APPLE_SMC = 4,
    LAPTOP_VENDOR_HP_OMEN   = 5
} laptop_vendor_t;

typedef enum {
    FAN_MODE_AUTO       = 0,
    FAN_MODE_SILENT     = 1,
    FAN_MODE_BALANCED   = 2,
    FAN_MODE_PERFORMANCE= 3,
    FAN_MODE_TURBO      = 4,
    FAN_MODE_DISENGAGED = 5
} laptop_fan_mode_t;

typedef enum {
    RGB_EFFECT_STATIC       = 0,
    RGB_EFFECT_BREATHING    = 1,
    RGB_EFFECT_COLOR_CYCLE  = 2,
    RGB_EFFECT_RAINBOW      = 3,
    RGB_EFFECT_STROBE       = 4
} laptop_rgb_effect_t;

typedef struct {
    laptop_vendor_t vendor;
    char            product_name[64];
    u8              lid_closed;
    u8              tablet_mode;
    u8              thinklight_active;
    u8              trackpoint_enabled;
    u8              trackpoint_sensitivity; /* 0-255 */
    laptop_fan_mode_t fan_mode;
    u16             fan_rpm_primary;
    u16             fan_rpm_secondary;
    u16             cpu_temp_celsius;
    u16             gpu_temp_celsius;
    u8              keyboard_backlight_level; /* 0-3 or 0-255 */
    laptop_rgb_effect_t rgb_effect;
    u32             rgb_color_hex;          /* 0x00RRGGBB */
} laptop_platform_status_t;

/* ==============================================================================
 * Display Backlight & Ambient Light Sensor (ALS)
 * ============================================================================== */

typedef struct {
    u8  auto_brightness_enabled;
    u16 current_brightness;         /* 0 - 1000 */
    u16 max_brightness;             /* 1000 */
    u32 ambient_light_lux;          /* Ambient Light reading in Lux */
    u8  smooth_fade_active;
    u16 target_brightness;
} backlight_status_t;

/* ==============================================================================
 * CPU Power Management & DVFS (Dynamic Voltage and Frequency Scaling)
 * ============================================================================== */

typedef enum {
    CPU_GOVERNOR_PERFORMANCE    = 0,
    CPU_GOVERNOR_POWERSAVE      = 1,
    CPU_GOVERNOR_BALANCED       = 2,
    CPU_GOVERNOR_AUTONOMOUS_HWP = 3
} cpu_power_governor_t;

typedef struct {
    cpu_power_governor_t governor;
    u8  epp_level;                  /* Energy-Performance Preference: 0=max perf, 128=balance, 255=max power save */
    u32 min_frequency_mhz;
    u32 max_frequency_mhz;
    u32 current_frequency_mhz;
    u32 turbo_boost_enabled;
    u32 thermal_throttle_count;
    u32 package_temp_celsius;
} cpu_power_status_t;

/* ==============================================================================
 * Laptop Platform Subsystem APIs
 * ============================================================================== */

/* ACPI Embedded Controller API */
void acpi_ec_init(void);
int  acpi_ec_read(u8 address, u8* data);
int  acpi_ec_write(u8 address, u8 data);
int  acpi_ec_burst_enable(void);
int  acpi_ec_burst_disable(void);
u8   acpi_ec_query_event(void);
void acpi_ec_poll(void);

/* Battery & SBS Subsystem API */
void battery_sbs_init(void);
void battery_sbs_poll(void);
int  battery_sbs_get_info(battery_info_t* out);
u8   battery_sbs_is_charging(void);
u8   battery_sbs_get_percentage(void);

/* Vendor Platform Drivers API */
void laptop_vendors_init(void);
void laptop_vendors_poll(void);
int  laptop_set_fan_mode(laptop_fan_mode_t mode);
int  laptop_set_keyboard_backlight(u8 level);
int  laptop_set_rgb_keyboard(laptop_rgb_effect_t effect, u32 color_hex);
int  laptop_set_thinklight(u8 enabled);
int  laptop_set_trackpoint_sensitivity(u8 sensitivity);
void laptop_get_platform_status(laptop_platform_status_t* out);

/* Backlight & ALS API */
void backlight_als_init(void);
void backlight_als_poll(void);
int  backlight_set_level(u16 level);
u16  backlight_get_level(void);
void backlight_set_auto_mode(u8 enabled);
void backlight_get_status(backlight_status_t* out);

/* CPU Power & DVFS API */
void cpu_power_init(void);
void cpu_power_poll(void);
int  cpu_power_set_governor(cpu_power_governor_t gov);
int  cpu_power_set_epp(u8 epp);
int  cpu_power_set_turbo(u8 enabled);
void cpu_power_get_status(cpu_power_status_t* out);

#endif /* LAPTOP_PLATFORM_H */
