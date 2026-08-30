#ifndef SOUND_H
#define SOUND_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOUND_DRIVER_NONE      0
#define SOUND_DRIVER_HDA       1
#define SOUND_DRIVER_AC97      2
#define SOUND_DRIVER_PCSPEAKER 3

void        sound_init(void);
int         sound_is_available(void);
const char* sound_get_driver_name(void);
int         sound_set_volume(u8 volume_pct);
int         sound_play_pcm(const void* pcm_data, u32 len_bytes, u32 sample_rate, u8 channels);
int         sound_beep(u16 freq_hz, u32 ms);

#ifdef __cplusplus
}
#endif

#endif /* SOUND_H */
