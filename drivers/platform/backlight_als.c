#include "laptop_platform.h"
#include "spinlock.h"
#include "timer.h"

/* ==============================================================================
 * Display Backlight & Ambient Light Sensor (ALS) Driver
 * ============================================================================== */

static backlight_status_t s_bl;
static spinlock_t s_bl_lock;

void backlight_als_init(void) {
    spinlock_init(&s_bl_lock);
    u32 flags = spinlock_lock_irqsave(&s_bl_lock);

    s_bl.auto_brightness_enabled = 1;
    s_bl.current_brightness = 750; /* 75% */
    s_bl.max_brightness = 1000;
    s_bl.ambient_light_lux = 320;  /* 320 Lux (Standard indoor lighting) */
    s_bl.smooth_fade_active = 0;
    s_bl.target_brightness = 750;

    spinlock_unlock_irqrestore(&s_bl_lock, flags);
}

void backlight_als_poll(void) {
    u32 flags = spinlock_lock_irqsave(&s_bl_lock);

    if (s_bl.auto_brightness_enabled) {
        /* Calculate target brightness based on ambient lux */
        u16 target = 300;
        if (s_bl.ambient_light_lux < 50) {
            target = 200; /* Dim room */
        } else if (s_bl.ambient_light_lux < 200) {
            target = 450;
        } else if (s_bl.ambient_light_lux < 600) {
            target = 700;
        } else {
            target = 1000; /* Bright sunlight */
        }
        s_bl.target_brightness = target;
    }

    /* Smooth Fade Step */
    if (s_bl.current_brightness < s_bl.target_brightness) {
        s_bl.current_brightness += 10;
        if (s_bl.current_brightness > s_bl.target_brightness) s_bl.current_brightness = s_bl.target_brightness;
    } else if (s_bl.current_brightness > s_bl.target_brightness) {
        s_bl.current_brightness -= 10;
        if (s_bl.current_brightness < s_bl.target_brightness) s_bl.current_brightness = s_bl.target_brightness;
    }

    spinlock_unlock_irqrestore(&s_bl_lock, flags);
}

int backlight_set_level(u16 level) {
    u32 flags = spinlock_lock_irqsave(&s_bl_lock);
    if (level > s_bl.max_brightness) level = s_bl.max_brightness;
    s_bl.auto_brightness_enabled = 0; /* Manual override disables auto-dimmer */
    s_bl.target_brightness = level;
    s_bl.current_brightness = level;
    spinlock_unlock_irqrestore(&s_bl_lock, flags);
    return 0;
}

u16 backlight_get_level(void) {
    u32 flags = spinlock_lock_irqsave(&s_bl_lock);
    u16 lvl = s_bl.current_brightness;
    spinlock_unlock_irqrestore(&s_bl_lock, flags);
    return lvl;
}

void backlight_set_auto_mode(u8 enabled) {
    u32 flags = spinlock_lock_irqsave(&s_bl_lock);
    s_bl.auto_brightness_enabled = enabled ? 1 : 0;
    spinlock_unlock_irqrestore(&s_bl_lock, flags);
}

void backlight_get_status(backlight_status_t* out) {
    if (!out) return;
    u32 flags = spinlock_lock_irqsave(&s_bl_lock);
    *out = s_bl;
    spinlock_unlock_irqrestore(&s_bl_lock, flags);
}
