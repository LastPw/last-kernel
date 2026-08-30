#ifndef DRIVERS_AUDIO_MICROPHONE_H
#define DRIVERS_AUDIO_MICROPHONE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Microphone Polar Pick-Up Patterns ──────────────────────────────────── */
typedef enum {
    MIC_POLAR_CARDIOID       = 0, /* Frontal Focus, Rear Rejection (Solo Streamer/Vocal) */
    MIC_POLAR_SUPERCARDIOID  = 1, /* Narrow Front Angle, Heavy Ambient Suppression */
    MIC_POLAR_OMNIDIRECTIONAL = 2, /* 360-Degree Ambient Room Capture */
    MIC_POLAR_BIDIRECTIONAL  = 3, /* Figure-8 (Front & Back, Face-to-Face Interview) */
} mic_polar_pattern_t;

/* ─── Noise Suppression Aggressiveness Modes ─────────────────────────────── */
typedef enum {
    MIC_NOISE_SUPPRESSION_OFF      = 0,
    MIC_NOISE_SUPPRESSION_MILD     = 1, /* 6 dB attenuation */
    MIC_NOISE_SUPPRESSION_MODERATE = 2, /* 12 dB attenuation (Default) */
    MIC_NOISE_SUPPRESSION_AGGRESSIVE = 3, /* 18 dB attenuation (Heavy HVAC/Key-clicks) */
} mic_ns_mode_t;

/* ─── Dynamic Voice Activity Detection State ─────────────────────────────── */
typedef struct {
    u8  is_speech_active;      /* 1 = Human Voice Detected, 0 = Silence/Background Noise */
    u16 speech_probability_pct;/* 0..100 % confidence */
    u32 short_time_energy;     /* STE metric */
    u32 zero_crossing_rate;    /* ZCR metric for consonants/fricatives */
    u32 spectral_flatness_q16; /* Tonal pitch vs White noise indicator */
    u16 hangover_counter;      /* Prevents clipping during sentence pauses */
} mic_vad_state_t;

/* ─── Microphone Subsystem Hardware & DSP State ──────────────────────────── */
typedef struct {
    u8                  is_initialized;
    u8                  is_capturing;
    u8                  hardware_muted;
    u8                  gain_pct;               /* 0..100 (0dB to +30dB Hardware Preamp) */
    u8                  boost_db;               /* 0dB, +10dB, +20dB, +30dB Boost */
    u8                  phantom_power_active;   /* +48V Bias Active for Studio Condenser Mics */
    mic_polar_pattern_t polar_pattern;
    mic_ns_mode_t       ns_mode;
    u8                  echo_cancel_enabled;    /* Acoustic Echo Cancellation (AEC) */
    u8                  agc_enabled;            /* Automatic Gain Control */
    s8                  agc_target_dbfs;        /* e.g., -18 dBFS Broadcast Standard */
    u8                  high_pass_80hz_enabled; /* Butterworth 80Hz rumble cut */
    u8                  de_esser_enabled;       /* Harsh sibilance suppressor (5-8kHz) */
    mic_vad_state_t     vad;
    u32                 sample_rate;            /* 16000, 44100, 48000, 96000 */
    u8                  channels;               /* 1 = Mono, 2 = Stereo Array */
    u64                 total_samples_captured;
    u32                 peak_level_dbfs;
    u32                 rms_level_dbfs;
    u32                 estimated_snr_db;
} microphone_driver_t;

/* ─── Public Microphone APIs ─────────────────────────────────────────────── */
int  mic_init(void);
int  mic_is_available(void);
int  mic_start_capture(u32 sample_rate, u8 channels);
int  mic_stop_capture(void);
int  mic_set_gain(u8 gain_pct);
u8   mic_get_gain(void);
int  mic_set_boost(u8 boost_db);
int  mic_set_mute(u8 mute);
int  mic_set_polar_pattern(mic_polar_pattern_t pattern);

/* DSP & Filtering Controls */
int  mic_set_noise_suppression(mic_ns_mode_t mode);
int  mic_enable_echo_cancellation(u8 enable);
int  mic_enable_agc(u8 enable, s8 target_dbfs);
int  mic_enable_highpass_80hz(u8 enable);
int  mic_enable_de_esser(u8 enable);

/* Capture Ring Buffer & Real-Time Processing Pipeline */
int  mic_process_capture_frame(const s16* in_raw_adc, s16* out_processed, u32 sample_count);
int  mic_read_pcm(s16* out_buffer, u32 max_samples);
int  mic_export_recorded_wav(u8* out_wav_buffer, u32 max_buffer_bytes, u32* out_actual_bytes);
int  mic_feed_reference_playback(const s16* ref_audio, u32 sample_count); /* AEC Reference */
void mic_handle_jack_interrupt(u8 is_plugged);
void mic_get_status(void);
void mic_dump_telemetry(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_AUDIO_MICROPHONE_H */
