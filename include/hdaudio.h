#ifndef HDAUDIO_H
#define HDAUDIO_H

#include "types.h"

#define HDAUDIO_MMIO_BASE 0xFED08000ULL

void hdaudio_init(void);
int hdaudio_available(void);
int hdaudio_play_pcm(const u8* pcm_data, u32 len, u32 sample_rate, u8 channels);

#endif /* HDAUDIO_H */
