#ifndef DRIVERS_AUDIO_HEADPHONE_H
#define DRIVERS_AUDIO_HEADPHONE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Hardware Impedance Load Classes ────────────────────────────────────── */
typedef enum {
    HP_IMPEDANCE_LOW_IEM    = 0, /* < 24 Ohm (In-Ear Monitors, Ultra-Sensitive) */
    HP_IMPEDANCE_CONSUMER   = 1, /* 24 - 100 Ohm (Standard Consumer Cans) */
    HP_IMPEDANCE_AUDIOPHILE = 2, /* 100 - 300 Ohm (High-Impedance Dynamic) */
    HP_IMPEDANCE_STUDIO_600 = 3, /* > 300 Ohm (600-Ohm Studio Reference) */
} hp_impedance_class_t;

/* ─── Equalizer Preset Profiles ──────────────────────────────────────────── */
typedef enum {
    HP_EQ_PROFILE_FLAT           = 0, /* Bit-Perfect Reference Neutral */
    HP_EQ_PROFILE_HARMAN_TARGET  = 1, /* Harman 2019 Acoustic Curve */
    HP_EQ_PROFILE_BASS_BOOST     = 2, /* Sub-Bass & Mid-Bass Punch (+9dB) */
    HP_EQ_PROFILE_VOCAL_CLARITY  = 3, /* Mid-Range Presence & Dialogue Focus */
    HP_EQ_PROFILE_SPATIAL_GAMING = 4, /* Footstep & Pinpoint Spatial Awareness */
    HP_EQ_PROFILE_CUSTOM         = 5, /* User-Defined 10-Band Coefficients */
} hp_eq_profile_t;

/* ─── 3D Spatial Audio Coordinate ────────────────────────────────────────── */
typedef struct {
    i32 azimuth_deg;   /* -180 to +180 degrees (0 = Directly Ahead) */
    i32 elevation_deg; /* -90 (Below) to +90 (Above) */
    u32 distance_cm;   /* 10cm to 10,000cm */
} hp_spatial_pos_t;

/* ─── Biquad Filter Descriptor ───────────────────────────────────────────── */
typedef struct {
    i32 b0, b1, b2;    /* Numerator coefficients (Q14 fixed-point) */
    i32 a1, a2;        /* Denominator coefficients (Q14 fixed-point) */
    i32 x1, x2;        /* Direct Form II history state */
    i32 y1, y2;
} hp_biquad_t;

/* ─── Audiophile Headphone Subsystem State ───────────────────────────────── */
typedef struct {
    u8                   is_initialized;
    u8                   jack_inserted;
    u8                   master_mute;
    u8                   volume_l;          /* 0..100 */
    u8                   volume_r;          /* 0..100 */
    s8                   channel_balance;   /* -50 (Left) to +50 (Right) */
    u16                  detected_ohms;     /* Measured Load (16..600 Ohms) */
    hp_impedance_class_t impedance_mode;
    u8                   anti_pop_active;
    u8                   crossfeed_enabled;
    u8                   crossfeed_level;   /* 1 (Subtle), 2 (Medium), 3 (Max) */
    u8                   spatial_3d_enabled;
    hp_spatial_pos_t     spatial_pos;
    hp_eq_profile_t      active_profile;
    s8                   eq_bands_db[10];   /* -12dB to +12dB per band */
    hp_biquad_t          eq_filters_l[10];
    hp_biquad_t          eq_filters_r[10];
    u8                   limiter_enabled;
    u8                   hearing_protection_enabled;
    u32                  sample_rate;       /* 44100, 48000, 96000, 192000, 384000 */
    u64                  total_samples_played;
    u32                  peak_meter_dbfs_l;
    u32                  peak_meter_dbfs_r;
} headphone_driver_t;

/* ─── Public Driver APIs ─────────────────────────────────────────────────── */
int  headphone_init(void);
int  headphone_is_available(void);
int  headphone_set_volume(u8 volume_pct);
u8   headphone_get_volume(void);
int  headphone_set_balance(s8 balance);
int  headphone_set_mute(u8 mute);
int  headphone_detect_impedance(u16* out_ohms, hp_impedance_class_t* out_class);
int  headphone_set_impedance_override(hp_impedance_class_t mode);

/* DSP & Spatial Audio APIs */
int  headphone_enable_crossfeed(u8 enable, u8 level);
int  headphone_set_spatial_3d(u8 enable, i32 azimuth_deg, i32 elevation_deg, u32 distance_cm);
int  headphone_set_eq_profile(hp_eq_profile_t profile);
int  headphone_set_custom_eq_band(u8 band_idx, s8 gain_db);
int  headphone_enable_limiter(u8 enable, u8 hearing_protect);

/* Audio Processing & Hardware Streaming Pipeline */
int  headphone_process_pcm_stereo(const s16* in_stereo, s16* out_stereo, u32 sample_count);
int  headphone_play_stream(const s16* pcm_stereo_data, u32 sample_count, u32 sample_rate);
void headphone_handle_jack_interrupt(u8 is_inserted);
void headphone_get_status(void);
void headphone_dump_telemetry(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_AUDIO_HEADPHONE_H */
