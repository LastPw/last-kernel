#ifndef PCSPEAKER_H
#define PCSPEAKER_H

#include "types.h"

void pcspeaker_init(void);
void pcspeaker_play_tone(u32 frequency_hz);
void pcspeaker_nosound(void);
void pcspeaker_beep(u32 frequency_hz, u32 duration_ms);
void pcspeaker_play_startup_chime(void);
void pcspeaker_play_success_chime(void);

#endif
