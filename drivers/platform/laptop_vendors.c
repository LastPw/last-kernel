#include "laptop_platform.h"
#include "string.h"
#include "spinlock.h"
#include "timer.h"
#include "screen.h"

#ifndef HOST_TEST
#include "bootinfo.h"
#endif

/* ==============================================================================
 * Multi-Vendor Laptop Platform Driver
 * Supports: Real SMBIOS/DMI Auto-Detection, ThinkPad, ASUS ROG, Dell XPS, Apple SMC
 * ============================================================================== */

static laptop_platform_status_t s_plat;
static spinlock_t s_plat_lock;

#ifndef HOST_TEST
static const char* smbios_get_string(const u8* struct_start, u8 length, u8 str_index) {
    if (str_index == 0) return "";
    const char* str = (const char*)(struct_start + length);
    u8 current_index = 1;

    while (*str) {
        if (current_index == str_index) return str;
        /* Move to next null-terminated string */
        while (*str) str++;
        str++; /* Skip null terminator */
        current_index++;
    }
    return "";
}

static int str_contains_case(const char* haystack, const char* needle) {
    if (!haystack || !needle) return 0;
    u32 nlen = 0;
    while (needle[nlen]) nlen++;
    if (nlen == 0) return 1;

    for (u32 i = 0; haystack[i]; i++) {
        u32 j = 0;
        while (needle[j]) {
            char h = haystack[i + j];
            char n = needle[j];
            if (h >= 'A' && h <= 'Z') h = (char)(h + ('a' - 'A'));
            if (n >= 'A' && n <= 'Z') n = (char)(n + ('a' - 'A'));
            if (h != n) break;
            j++;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

static void probe_smbios_platform(void) {
    /* Scan physical BIOS/UEFI ROM area 0x000F0000..0x000FFFFF for SMBIOS Entry Point */
    for (u32 p = 0x000F0000; p < 0x00100000; p += 16) {
        const u8* entry = (const u8*)(g_hhdm_offset + p);
        if (entry[0] == '_' && entry[1] == 'S' && entry[2] == 'M' && entry[3] == '_') {
            u8 len = entry[5];
            if (len < 0x1F) continue;
            u8 sum = 0;
            for (u8 i = 0; i < len; i++) sum = (u8)(sum + entry[i]);
            if (sum != 0) continue;

            u32 table_phys = *(const u32*)(entry + 0x18);
            u16 table_len = *(const u16*)(entry + 0x16);
            if (table_phys == 0 || table_len == 0) continue;

            const u8* cur = (const u8*)(g_hhdm_offset + table_phys);
            const u8* table_end = cur + table_len;

            while (cur + 4 < table_end) {
                u8 type = cur[0];
                u8 hlen = cur[1];
                if (hlen < 4) break;

                if (type == 1 && hlen >= 6) { /* Type 1: System Information */
                    u8 mfg_idx = cur[4];
                    u8 prod_idx = cur[5];
                    const char* mfg = smbios_get_string(cur, hlen, mfg_idx);
                    const char* prod = smbios_get_string(cur, hlen, prod_idx);

                    if (prod[0]) {
                        str_ncpy(s_plat.product_name, prod, sizeof(s_plat.product_name));
                    }

                    if (str_contains_case(mfg, "lenovo") || str_contains_case(prod, "thinkpad")) {
                        s_plat.vendor = LAPTOP_VENDOR_THINKPAD;
                        s_plat.trackpoint_enabled = 1;
                    } else if (str_contains_case(mfg, "asus")) {
                        s_plat.vendor = LAPTOP_VENDOR_ASUS_ROG;
                        s_plat.trackpoint_enabled = 0;
                    } else if (str_contains_case(mfg, "dell")) {
                        s_plat.vendor = LAPTOP_VENDOR_DELL_XPS;
                        s_plat.trackpoint_enabled = 0;
                    } else if (str_contains_case(mfg, "apple")) {
                        s_plat.vendor = LAPTOP_VENDOR_APPLE_SMC;
                        s_plat.trackpoint_enabled = 0;
                    } else {
                        s_plat.vendor = LAPTOP_VENDOR_UNIVERSAL_ACPI;
                        s_plat.trackpoint_enabled = 0;
                    }
                    return;
                }

                if (type == 127) break; /* End-of-Table indicator */

                /* Move past formatted area + string block */
                const u8* next = cur + hlen;
                while (next + 1 < table_end && !(next[0] == 0 && next[1] == 0)) next++;
                cur = next + 2; /* Skip terminating double-null */
            }
        }
    }
}
#endif

void laptop_vendors_init(void) {
    spinlock_init(&s_plat_lock);
    u32 flags = spinlock_lock_irqsave(&s_plat_lock);

    /* Default baseline profile */
    s_plat.vendor = LAPTOP_VENDOR_THINKPAD;
    str_ncpy(s_plat.product_name, "ThinkPad X1 Carbon Gen 10", sizeof(s_plat.product_name));
    s_plat.lid_closed = 0;
    s_plat.tablet_mode = 0;
    s_plat.thinklight_active = 0;
    s_plat.trackpoint_enabled = 1;
    s_plat.trackpoint_sensitivity = 180;
    s_plat.fan_mode = FAN_MODE_BALANCED;
    s_plat.fan_rpm_primary = 2450;
    s_plat.fan_rpm_secondary = 2300;
    s_plat.cpu_temp_celsius = 45;
    s_plat.gpu_temp_celsius = 42;
    s_plat.keyboard_backlight_level = 2; /* Medium */
    s_plat.rgb_effect = RGB_EFFECT_STATIC;
    s_plat.rgb_color_hex = 0x0000FF;     /* Blue */

#ifndef HOST_TEST
    probe_smbios_platform();
#endif

    spinlock_unlock_irqrestore(&s_plat_lock, flags);
}

void laptop_vendors_poll(void) {
    u32 flags = spinlock_lock_irqsave(&s_plat_lock);

    /* Update thermal reading from EC */
    u8 raw_temp = 0;
    if (acpi_ec_read(0x02, &raw_temp) == 0 && raw_temp > 20 && raw_temp < 110) {
        s_plat.cpu_temp_celsius = raw_temp;
    }

    /* Fan Speed Dynamic Adjustment based on Profile */
    if (s_plat.fan_mode == FAN_MODE_SILENT) {
        s_plat.fan_rpm_primary = (s_plat.cpu_temp_celsius > 70) ? 2200 : 0;
    } else if (s_plat.fan_mode == FAN_MODE_PERFORMANCE) {
        s_plat.fan_rpm_primary = 3800;
    } else if (s_plat.fan_mode == FAN_MODE_TURBO) {
        s_plat.fan_rpm_primary = 5400;
    } else {
        /* Balanced dynamic curve */
        s_plat.fan_rpm_primary = (u16)(1500 + (s_plat.cpu_temp_celsius * 30));
    }
    s_plat.fan_rpm_secondary = s_plat.fan_rpm_primary;

    spinlock_unlock_irqrestore(&s_plat_lock, flags);
}

int laptop_set_fan_mode(laptop_fan_mode_t mode) {
    u32 flags = spinlock_lock_irqsave(&s_plat_lock);
    s_plat.fan_mode = mode;

    /* Write EC Fan Register */
    if (mode == FAN_MODE_DISENGAGED || mode == FAN_MODE_TURBO) {
        acpi_ec_write(0x2F, 0x40); /* Disengaged Full Blast */
    } else if (mode == FAN_MODE_SILENT) {
        acpi_ec_write(0x2F, 0x01); /* Low speed */
    } else {
        acpi_ec_write(0x2F, 0x80); /* Auto EC Controlled */
    }

    spinlock_unlock_irqrestore(&s_plat_lock, flags);
    return 0;
}

int laptop_set_keyboard_backlight(u8 level) {
    u32 flags = spinlock_lock_irqsave(&s_plat_lock);
    s_plat.keyboard_backlight_level = (level > 3) ? 3 : level;

    /* Write to ACPI / WMI keyboard illumination register */
    acpi_ec_write(0x31, s_plat.keyboard_backlight_level);

    spinlock_unlock_irqrestore(&s_plat_lock, flags);
    return 0;
}

int laptop_set_rgb_keyboard(laptop_rgb_effect_t effect, u32 color_hex) {
    u32 flags = spinlock_lock_irqsave(&s_plat_lock);
    s_plat.rgb_effect = effect;
    s_plat.rgb_color_hex = color_hex;
    spinlock_unlock_irqrestore(&s_plat_lock, flags);
    return 0;
}

int laptop_set_thinklight(u8 enabled) {
    u32 flags = spinlock_lock_irqsave(&s_plat_lock);
    s_plat.thinklight_active = enabled ? 1 : 0;
    acpi_ec_write(0x3B, s_plat.thinklight_active ? 0x01 : 0x00);
    spinlock_unlock_irqrestore(&s_plat_lock, flags);
    return 0;
}

int laptop_set_trackpoint_sensitivity(u8 sensitivity) {
    u32 flags = spinlock_lock_irqsave(&s_plat_lock);
    s_plat.trackpoint_sensitivity = sensitivity;
    spinlock_unlock_irqrestore(&s_plat_lock, flags);
    return 0;
}

void laptop_get_platform_status(laptop_platform_status_t* out) {
    if (!out) return;
    u32 flags = spinlock_lock_irqsave(&s_plat_lock);
    *out = s_plat;
    spinlock_unlock_irqrestore(&s_plat_lock, flags);
}
