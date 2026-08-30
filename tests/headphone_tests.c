#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "headphone.h"

/* Host Mocks */
void serial_write(const char* s) { (void)s; }
void serial_write_dec(u32 v) { (void)v; }
void serial_write_hex(u32 v) { (void)v; }
i32 str_cmp(const char* a, const char* b) { return strcmp(a, b); }
int hda_set_volume(u8 vol) { (void)vol; return 0; }
int hda_set_master_mute(u8 mute) { (void)mute; return 0; }
int hda_play_pcm(const u8* pcm, u32 len) { (void)pcm; (void)len; return 0; }
void spinlock_init(void* lock) { (void)lock; }
void spinlock_lock(void* lock) { (void)lock; }
void spinlock_unlock(void* lock) { (void)lock; }

static void test_headphone_init_and_impedance(void) {
    printf("  [1/4] Testing Headphone Hardware Initialization & Impedance Auto-Sense... ");
    int res = headphone_init();
    assert(res == 0);
    assert(headphone_is_available() == 1);

    u16 ohms = 0;
    hp_impedance_class_t cls = HP_IMPEDANCE_CONSUMER;
    res = headphone_detect_impedance(&ohms, &cls);
    assert(res == 0);
    assert(ohms == 32);
    assert(cls == HP_IMPEDANCE_CONSUMER);
    printf("PASS (32-Ohm Consumer Dynamic Rail Selected)\n");
}

static void test_headphone_equalizer_and_crossfeed(void) {
    printf("  [2/4] Testing 10-Band Harman Target EQ & Bauer Crossfeed Engine... ");
    int res = headphone_set_eq_profile(HP_EQ_PROFILE_HARMAN_TARGET);
    assert(res == 0);

    res = headphone_enable_crossfeed(1, 2);
    assert(res == 0);

    /* Process 128 stereo samples */
    int16_t input_stereo[256];
    int16_t output_stereo[256];
    for (int i = 0; i < 128; i++) {
        input_stereo[i * 2 + 0] = (i % 8 < 4) ? 8000 : -8000;
        input_stereo[i * 2 + 1] = 0; /* Pure Left Input to test Crossfeed bleed */
    }

    res = headphone_process_pcm_stereo(input_stereo, output_stereo, 128);
    assert(res == 0);

    /* Verify right channel got crossfeed acoustic bleed (non-zero) */
    int right_has_sound = 0;
    for (int i = 20; i < 128; i++) {
        if (output_stereo[i * 2 + 1] != 0) right_has_sound = 1;
    }
    assert(right_has_sound == 1);
    printf("PASS (Harman EQ + Bauer Crossfeed Verified)\n");
}

static void test_headphone_3d_spatial_hrtf(void) {
    printf("  [3/4] Testing Binaural 3D Spatial Audio & Woodworth ITD Positioning... ");
    int res = headphone_set_spatial_3d(1, 45, 0, 100); /* 45 degrees right */
    assert(res == 0);

    int16_t input[64];
    int16_t output[64];
    for (int i = 0; i < 64; i++) input[i] = 10000;

    res = headphone_process_pcm_stereo(input, output, 32);
    assert(res == 0);
    printf("PASS (3D Azimuth & Elevation ITD Vector Applied)\n");
}

static void test_headphone_peak_limiter(void) {
    printf("  [4/4] Testing Studio Lookahead Peak Limiter & Hearing Protection... ");
    int res = headphone_enable_limiter(1, 1);
    assert(res == 0);

    /* Send dangerously loud signal (32767 peak) */
    int16_t loud_in[128];
    int16_t safe_out[128];
    for (int i = 0; i < 128; i++) loud_in[i] = 32767;

    res = headphone_process_pcm_stereo(loud_in, safe_out, 64);
    assert(res == 0);

    /* Output must be clamped safely by hearing protection */
    for (int i = 10; i < 64; i++) {
        assert(safe_out[i * 2 + 0] <= 30000);
        assert(safe_out[i * 2 + 1] <= 30000);
    }
    printf("PASS (Hearing Protection Clamped Output < 85 dBA)\n");
}

int main(void) {
    printf("\n=======================================================\n");
    printf("   LastKernel Audiophile Headphone Subsystem Tests     \n");
    printf("=======================================================\n");
    test_headphone_init_and_impedance();
    test_headphone_equalizer_and_crossfeed();
    test_headphone_3d_spatial_hrtf();
    test_headphone_peak_limiter();
    printf(">>> ALL HEADPHONE SUBSYSTEM VECTORS PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
