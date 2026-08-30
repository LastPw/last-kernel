#ifndef AC97_H
#define AC97_H

#include "types.h"

#define AC97_VENDOR_INTEL  0x8086u
#define AC97_DEVICE_ICH    0x2415u

void ac97_init(void);
int  ac97_available(void);
int  ac97_set_volume(u8 left, u8 right);
int  ac97_play_pcm(const void* pcm_data, u32 len_bytes, u32 sample_rate, u8 channels);
/* Short utility tones (16-bit LE, generated internally). */
int  ac97_beep(u16 freq_hz, u32 ms);
u32  ac97_sample_rate(void);

#endif
