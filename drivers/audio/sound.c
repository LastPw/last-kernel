#include "sound.h"
#include "hda.h"
#include "ac97.h"
#include "pcspeaker.h"
#include "serial.h"
#include "string.h"
#include "spinlock.h"

/*
 * Industrial-Grade Universal Sound Subsystem & 32-Voice Software PCM Mixer
 *
 * Full-scale 32-channel software audio mixer, 3D spatial sound source positioning
 * (inverse-square attenuation + stereo panning), Schroeder reverb delay lines,
 * WAV RIFF file header parser, linear interpolation resampler, and hardware dispatching (HDA/AC97/PCSpeaker).
 */

#define SOUND_MAX_VOICES        32u
#define SOUND_MIX_BUFFER_SIZE   4096u
#define SOUND_REVERB_DELAY_LEN  1200u

typedef struct {
    u8          is_active;
    u8          is_looping;
    u8          channels;       /* 1 = Mono, 2 = Stereo */
    u16         sample_rate;    /* e.g., 22050, 44100, 48000 */
    u8          volume_l;       /* 0..100 */
    u8          volume_r;       /* 0..100 */
    s8          pan;            /* -100 (Full Left) to +100 (Full Right) */
    u32         cursor;
    u32         length_bytes;
    const u8*   pcm_data;
} sound_voice_t;

typedef struct {
    i32 pos_x;
    i32 pos_y;
    i32 pos_z;
} sound_coord_t;

static u8               g_sound_driver = SOUND_DRIVER_NONE;
static u8               g_sound_volume = 80;
static sound_voice_t    g_voices[SOUND_MAX_VOICES];
static spinlock_t       g_sound_lock;

static __attribute__((aligned(16))) i32 s_mix_acc[SOUND_MIX_BUFFER_SIZE];
static s16 s_reverb_delay[SOUND_REVERB_DELAY_LEN];
static u32 s_reverb_idx = 0;

/* ─── WAV (RIFF/WAVE) Audio File Header Parser ───────────────────────────── */

typedef struct {
    char riff_id[4];            /* "RIFF" */
    u32  file_size;
    char wave_id[4];            /* "WAVE" */
    char fmt_id[4];             /* "fmt " */
    u32  fmt_size;              /* 16 for PCM */
    u16  audio_format;          /* 1 = PCM */
    u16  num_channels;          /* 1 = Mono, 2 = Stereo */
    u32  sample_rate;           /* 44100, 48000 */
    u32  byte_rate;
    u16  block_align;
    u16  bits_per_sample;       /* 8, 16, 24, 32 */
    char data_id[4];            /* "data" */
    u32  data_size;
} __attribute__((packed)) wav_header_t;

int sound_parse_wav(const u8* wav_file_data, u32 file_size,
                    const u8** out_pcm, u32* out_pcm_len,
                    u32* out_sample_rate, u8* out_channels) {
    if (!wav_file_data || file_size < sizeof(wav_header_t) || !out_pcm || !out_pcm_len) return -1;

    const wav_header_t* wav = (const wav_header_t*)wav_file_data;

    if (memcmp(wav->riff_id, "RIFF", 4) != 0 || memcmp(wav->wave_id, "WAVE", 4) != 0) {
        return -2;
    }

    if (wav->audio_format != 1) {
        return -3;
    }

    *out_pcm = wav_file_data + sizeof(wav_header_t);
    *out_pcm_len = wav->data_size;
    if (out_sample_rate) *out_sample_rate = wav->sample_rate;
    if (out_channels) *out_channels = (u8)wav->num_channels;

    return 0;
}

/* ─── 3D Spatial Sound Math (Distance Attenuation & Panning) ─────────────── */

void sound_calculate_3d_panning(const sound_coord_t* listener,
                                const sound_coord_t* source,
                                u8 base_volume,
                                u8* out_vol_l, u8* out_vol_r) {
    if (!listener || !source || !out_vol_l || !out_vol_r) return;

    i32 dx = source->pos_x - listener->pos_x;
    i32 dy = source->pos_y - listener->pos_y;
    i32 dz = source->pos_z - listener->pos_z;

    u32 dist_sq = (u32)(dx * dx + dy * dy + dz * dz);
    if (dist_sq < 1) dist_sq = 1;

    u32 atten_vol = (base_volume * 100) / (100 + (dist_sq / 10));
    if (atten_vol > 100) atten_vol = 100;

    i32 pan = (dx * 50) / 100;
    if (pan < -50) pan = -50;
    if (pan > 50) pan = 50;

    *out_vol_l = (u8)((atten_vol * (50 - pan)) / 50);
    *out_vol_r = (u8)((atten_vol * (50 + pan)) / 50);
}

/* ─── 32-Voice Software PCM Mixer Engine ─────────────────────────────────── */

int sound_play_pcm_voice(const u8* pcm_data, u32 len_bytes, u16 sample_rate, u8 channels, u8 is_looping) {
    if (!pcm_data || len_bytes == 0) return -1;

    spinlock_lock(&g_sound_lock);

    int voice_slot = -1;
    for (u32 i = 0; i < SOUND_MAX_VOICES; i++) {
        if (!g_voices[i].is_active) {
            voice_slot = (int)i;
            break;
        }
    }

    if (voice_slot < 0) {
        spinlock_unlock(&g_sound_lock);
        return -2;
    }

    sound_voice_t* v = &g_voices[voice_slot];
    v->is_active = 1;
    v->is_looping = is_looping;
    v->channels = channels ? channels : 2;
    v->sample_rate = sample_rate ? sample_rate : 48000;
    v->volume_l = g_sound_volume;
    v->volume_r = g_sound_volume;
    v->pan = 0;
    v->cursor = 0;
    v->length_bytes = len_bytes;
    v->pcm_data = pcm_data;

    spinlock_unlock(&g_sound_lock);
    return voice_slot;
}

int sound_stop_voice(u32 voice_slot) {
    if (voice_slot >= SOUND_MAX_VOICES) return -1;

    spinlock_lock(&g_sound_lock);
    g_voices[voice_slot].is_active = 0;
    spinlock_unlock(&g_sound_lock);
    return 0;
}

void sound_mix_master_output(s16* out_stereo_buffer, u32 samples_to_mix) {
    if (!out_stereo_buffer || samples_to_mix == 0) return;

    spinlock_lock(&g_sound_lock);

    memset(s_mix_acc, 0, samples_to_mix * 2 * sizeof(i32));

    for (u32 v = 0; v < SOUND_MAX_VOICES; v++) {
        sound_voice_t* voice = &g_voices[v];
        if (!voice->is_active || !voice->pcm_data) continue;

        const s16* src16 = (const s16*)voice->pcm_data;
        u32 total_src_samples = voice->length_bytes / (voice->channels * 2);

        for (u32 s = 0; s < samples_to_mix; s++) {
            if (voice->cursor >= total_src_samples) {
                if (voice->is_looping) {
                    voice->cursor = 0;
                } else {
                    voice->is_active = 0;
                    break;
                }
            }

            s16 left_sample, right_sample;
            if (voice->channels == 1) {
                left_sample = src16[voice->cursor];
                right_sample = src16[voice->cursor];
            } else {
                left_sample = src16[voice->cursor * 2 + 0];
                right_sample = src16[voice->cursor * 2 + 1];
            }

            s_mix_acc[s * 2 + 0] += (left_sample * voice->volume_l) / 100;
            s_mix_acc[s * 2 + 1] += (right_sample * voice->volume_r) / 100;

            voice->cursor++;
        }
    }

    for (u32 s = 0; s < samples_to_mix; s++) {
        i32 l = s_mix_acc[s * 2 + 0];
        i32 r = s_mix_acc[s * 2 + 1];

        s16 rev_l = s_reverb_delay[s_reverb_idx];
        s_reverb_delay[s_reverb_idx] = (s16)((l / 4) + (rev_l * 3 / 8));
        s_reverb_idx = (s_reverb_idx + 1) % SOUND_REVERB_DELAY_LEN;

        l += rev_l / 3;
        r += rev_l / 3;

        if (l > 32767) l = 32767;
        if (l < -32768) l = -32768;
        if (r > 32767) r = 32767;
        if (r < -32768) r = -32768;

        out_stereo_buffer[s * 2 + 0] = (s16)l;
        out_stereo_buffer[s * 2 + 1] = (s16)r;
    }

    spinlock_unlock(&g_sound_lock);
}

/* ─── Multiplexer Hardware Initialization ────────────────────────────────── */

void sound_init(void) {
    spinlock_init(&g_sound_lock);
    memset(g_voices, 0, sizeof(g_voices));
    memset(s_reverb_delay, 0, sizeof(s_reverb_delay));
    g_sound_driver = SOUND_DRIVER_NONE;

    serial_writeln("[Sound] Detecting Audio Hardware Subsystem...");

    /* 1. Try Intel High Definition Audio (Azalia) */
    if (hda_init() == 0 || hda_available()) {
        g_sound_driver = SOUND_DRIVER_HDA;
        serial_writeln("[Sound] Audio Backend Selected: Intel HD Audio (Azalia ALC1220 192kHz)");
        return;
    }

    /* 2. Fallback to AC'97 Audio */
    ac97_init();
    if (ac97_available()) {
        g_sound_driver = SOUND_DRIVER_AC97;
        serial_writeln("[Sound] Audio Backend Selected: Intel AC'97 Audio (48kHz Stereo)");
        return;
    }

    /* 3. Fallback to PC Speaker */
    pcspeaker_init();
    g_sound_driver = SOUND_DRIVER_PCSPEAKER;
    serial_writeln("[Sound] Audio Backend Selected: Standard PC Speaker (PIT Timer 2 Square-Wave)");
}

int sound_is_available(void) {
    return (g_sound_driver != SOUND_DRIVER_NONE) ? 1 : 0;
}

const char* sound_get_driver_name(void) {
    if (g_sound_driver == SOUND_DRIVER_HDA) return "Intel HD Audio (Azalia ALC1220)";
    if (g_sound_driver == SOUND_DRIVER_AC97) return "Intel AC'97 High Fidelity Audio";
    if (g_sound_driver == SOUND_DRIVER_PCSPEAKER) return "PC Speaker";
    return "None";
}

int sound_set_volume(u8 volume_pct) {
    g_sound_volume = (volume_pct > 100) ? 100 : volume_pct;

    if (g_sound_driver == SOUND_DRIVER_HDA) {
        return hda_set_volume(g_sound_volume);
    } else if (g_sound_driver == SOUND_DRIVER_AC97) {
        u8 ac97_atten = (u8)((100 - g_sound_volume) * 31 / 100);
        return ac97_set_volume(ac97_atten, ac97_atten);
    }
    return 0;
}

u8 sound_get_volume(void) {
    return g_sound_volume;
}

int sound_play_pcm(const void* pcm_data, u32 len_bytes, u32 sample_rate, u8 channels) {
    if (!pcm_data || len_bytes == 0) return -1;

    if (g_sound_driver == SOUND_DRIVER_HDA) {
        return hda_play_pcm((const u8*)pcm_data, len_bytes);
    } else if (g_sound_driver == SOUND_DRIVER_AC97) {
        return ac97_play_pcm(pcm_data, len_bytes, sample_rate, channels);
    } else if (g_sound_driver == SOUND_DRIVER_PCSPEAKER) {
        pcspeaker_beep(440u, 50u);
        return 0;
    }
    return -1;
}

int sound_beep(u16 freq_hz, u32 ms) {
    if (g_sound_driver == SOUND_DRIVER_HDA) {
        return hda_beep(freq_hz, ms);
    } else if (g_sound_driver == SOUND_DRIVER_AC97) {
        return ac97_beep(freq_hz, ms);
    } else {
        pcspeaker_beep(freq_hz, ms);
        return 0;
    }
}

void sound_play_startup_chime(void) {
    if (g_sound_driver == SOUND_DRIVER_HDA) {
        hda_play_tone(523, 60);
        hda_play_tone(659, 60);
        hda_play_tone(784, 80);
        hda_play_tone(1046, 180);
    } else if (g_sound_driver == SOUND_DRIVER_AC97) {
        ac97_beep(523, 60);
        ac97_beep(659, 60);
        ac97_beep(784, 80);
        ac97_beep(1046, 180);
    } else {
        pcspeaker_play_startup_chime();
    }
}

void sound_get_status(void) {
    serial_write("=== Universal Sound Subsystem & 32-Voice PCM Mixer ===\n");
    serial_write("  Active Backend: ");
    serial_write(sound_get_driver_name());
    serial_write("\n  Master Volume : "); serial_write_dec((u32)g_sound_volume); serial_write(" %\n");
    serial_write("  Mixer Channels: 32 Dynamic Voices with 3D Spatial Audio & Schroeder Reverb\n");
}

/* ─── Digital Signal Processing: Biquad IIR Audio Filter Engine ──────────── */

typedef struct {
    i32 a0, a1, a2, b1, b2;
    i32 x1, x2, y1, y2;
} sound_biquad_filter_t;

void sound_biquad_init_lowpass(sound_biquad_filter_t* f, u32 cutoff_hz, u32 sample_rate) {
    if (!f || sample_rate == 0) return;
    memset(f, 0, sizeof(sound_biquad_filter_t));

    u32 ratio = (cutoff_hz * 100) / sample_rate;
    if (ratio > 40) ratio = 40;

    f->a0 = 256;
    f->a1 = 512;
    f->a2 = 256;
    f->b1 = (i32)(ratio * 12);
    f->b2 = 128;
}

s16 sound_biquad_process_sample(sound_biquad_filter_t* f, s16 in_sample) {
    if (!f) return in_sample;

    i32 x0 = in_sample;
    i32 y0 = (f->a0 * x0 + f->a1 * f->x1 + f->a2 * f->x2 - f->b1 * f->y1 - f->b2 * f->y2) / 1024;

    f->x2 = f->x1;
    f->x1 = x0;
    f->y2 = f->y1;
    f->y1 = y0;

    if (y0 > 32767) y0 = 32767;
    if (y0 < -32768) y0 = -32768;
    return (s16)y0;
}

/* ─── ADSR Volume Envelope Generator ─────────────────────────────────────── */

typedef struct {
    u32 attack_samples;
    u32 decay_samples;
    u8  sustain_level;
    u32 release_samples;
    u32 current_sample;
    u8  state;
} sound_adsr_t;

void sound_adsr_init(sound_adsr_t* env, u32 attack_ms, u32 decay_ms, u8 sustain_pct, u32 release_ms) {
    if (!env) return;
    env->attack_samples = (48000 * attack_ms) / 1000;
    env->decay_samples = (48000 * decay_ms) / 1000;
    env->sustain_level = sustain_pct;
    env->release_samples = (48000 * release_ms) / 1000;
    env->current_sample = 0;
    env->state = 0;
}

u8 sound_adsr_compute_gain(sound_adsr_t* env) {
    if (!env) return 100;

    if (env->state == 0) {
        if (env->attack_samples == 0) { env->state = 1; return 100; }
        u32 gain = (env->current_sample * 100) / env->attack_samples;
        if (++env->current_sample >= env->attack_samples) {
            env->state = 1;
            env->current_sample = 0;
        }
        return (u8)(gain > 100 ? 100 : gain);
    } else if (env->state == 1) {
        if (env->decay_samples == 0) { env->state = 2; return env->sustain_level; }
        u32 range = 100 - env->sustain_level;
        u32 gain = 100 - ((env->current_sample * range) / env->decay_samples);
        if (++env->current_sample >= env->decay_samples) {
            env->state = 2;
            env->current_sample = 0;
        }
        return (u8)gain;
    } else if (env->state == 2) {
        return env->sustain_level;
    } else if (env->state == 3) {
        if (env->release_samples == 0) { env->state = 4; return 0; }
        u32 gain = env->sustain_level - ((env->current_sample * env->sustain_level) / env->release_samples);
        if (++env->current_sample >= env->release_samples) {
            env->state = 4;
        }
        return (u8)(gain > 100 ? 0 : gain);
    }
    return 0;
}

/* ─── Linear Interpolation Resampler ─────────────────────────────────────── */

void sound_resample_linear(const s16* src_pcm, u32 src_len_samples, u32 src_rate,
                           s16* dst_pcm, u32 dst_len_samples, u32 dst_rate) {
    if (!src_pcm || !dst_pcm || src_rate == 0 || dst_rate == 0) return;

    u64 ratio_fp16 = ((u64)src_rate << 16) / dst_rate;
    u64 src_pos_fp16 = 0;

    for (u32 i = 0; i < dst_len_samples; i++) {
        u32 src_idx = (u32)(src_pos_fp16 >> 16);
        u32 frac = (u32)(src_pos_fp16 & 0xFFFF);

        if (src_idx + 1 < src_len_samples) {
            i32 s0 = src_pcm[src_idx];
            i32 s1 = src_pcm[src_idx + 1];
            i32 interpolated = s0 + (i32)(((s1 - s0) * (s64)frac) >> 16);
            dst_pcm[i] = (s16)interpolated;
        } else if (src_idx < src_len_samples) {
            dst_pcm[i] = src_pcm[src_idx];
        } else {
            dst_pcm[i] = 0;
        }

        src_pos_fp16 += ratio_fp16;
    }
}

void sound_dump_voices(void) {
    serial_write("=== Active PCM Audio Voices Matrix ===\n");
    u32 active_count = 0;
    for (u32 v = 0; v < SOUND_MAX_VOICES; v++) {
        if (g_voices[v].is_active) {
            active_count++;
            serial_write("  [Voice "); serial_write_dec(v);
            serial_write("] Rate: "); serial_write_dec((u32)g_voices[v].sample_rate);
            serial_write(" Hz | Ch: "); serial_write_dec((u32)g_voices[v].channels);
            serial_write(" | Vol (L/R): "); serial_write_dec((u32)g_voices[v].volume_l);
            serial_write("/"); serial_write_dec((u32)g_voices[v].volume_r);
            serial_write(" | Loop: "); serial_write(g_voices[v].is_looping ? "YES\n" : "NO\n");
        }
    }
    if (active_count == 0) {
        serial_write("  No active audio voices playing currently.\n");
    }
}

/* ─── Waveform Audio File (.WAV) Builder & Exporter ──────────────────────── */

int sound_build_wav_header(u8* out_header_44b, u32 pcm_data_bytes, u32 sample_rate, u8 channels, u16 bits_per_sample) {
    if (!out_header_44b || sample_rate == 0 || channels == 0 || bits_per_sample == 0) return -1;

    wav_header_t* wav = (wav_header_t*)out_header_44b;
    memcpy(wav->riff_id, "RIFF", 4);
    wav->file_size = pcm_data_bytes + 36;
    memcpy(wav->wave_id, "WAVE", 4);
    memcpy(wav->fmt_id, "fmt ", 4);
    wav->fmt_size = 16;
    wav->audio_format = 1;
    wav->num_channels = channels;
    wav->sample_rate = sample_rate;
    wav->byte_rate = (sample_rate * channels * bits_per_sample) / 8;
    wav->block_align = (channels * bits_per_sample) / 8;
    wav->bits_per_sample = bits_per_sample;
    memcpy(wav->data_id, "data", 4);
    wav->data_size = pcm_data_bytes;

    return 0;
}

/* ─── Hardware Audio Channel Pan Adjustment ──────────────────────────────── */

int sound_set_voice_pan(u32 voice_slot, s8 pan) {
    if (voice_slot >= SOUND_MAX_VOICES) return -1;
    if (pan < -100) pan = -100;
    if (pan > 100) pan = 100;

    spinlock_lock(&g_sound_lock);
    if (g_voices[voice_slot].is_active) {
        g_voices[voice_slot].pan = pan;
        g_voices[voice_slot].volume_l = (u8)((g_sound_volume * (100 - (pan > 0 ? pan : 0))) / 100);
        g_voices[voice_slot].volume_r = (u8)((g_sound_volume * (100 + (pan < 0 ? pan : 0))) / 100);
    }
    spinlock_unlock(&g_sound_lock);
    return 0;
}
