#include "pcspeaker.h"
#include "io.h"
#include "timer.h"
#include "serial.h"
#include "spinlock.h"

#define PIT_TIMER2_PORT    0x42u
#define PIT_COMMAND_PORT   0x43u
#define PIT_CONTROL_WORD   0xB6u   /* Channel 2, LSB/MSB, Square wave generator */
#define PC_SPEAKER_PORT    0x61u

static spinlock_t g_pcspeaker_lock;
static u8 g_pcspeaker_present = 0u;

void pcspeaker_init(void) {
    spinlock_init(&g_pcspeaker_lock);
    g_pcspeaker_present = 1u;
    serial_writeln("[PCSpeaker] PC Speaker Driver Initialized (Programmable Interval Timer Channel 2)");
}

void pcspeaker_play_tone(u32 frequency_hz) {
    if (!g_pcspeaker_present || frequency_hz == 0u) return;

    spinlock_lock(&g_pcspeaker_lock);
    u32 divisor = 1193182u / frequency_hz;
    if (divisor > 0xFFFFu) divisor = 0xFFFFu;

    outb(PIT_COMMAND_PORT, PIT_CONTROL_WORD);
    outb(PIT_TIMER2_PORT, (u8)(divisor & 0xFFu));
    outb(PIT_TIMER2_PORT, (u8)((divisor >> 8u) & 0xFFu));

    u8 curr = inb(PC_SPEAKER_PORT);
    if ((curr & 0x03u) != 0x03u) {
        outb(PC_SPEAKER_PORT, curr | 0x03u);
    }
    spinlock_unlock(&g_pcspeaker_lock);
}

void pcspeaker_nosound(void) {
    if (!g_pcspeaker_present) return;

    spinlock_lock(&g_pcspeaker_lock);
    u8 curr = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, curr & ~0x03u);
    spinlock_unlock(&g_pcspeaker_lock);
}

void pcspeaker_beep(u32 frequency_hz, u32 duration_ms) {
    pcspeaker_play_tone(frequency_hz);
    timer_delay_ms(duration_ms);
    pcspeaker_nosound();
}

void pcspeaker_play_startup_chime(void) {
    /* Harmonious 4-tone rising chord (C5 -> E5 -> G5 -> C6) */
    pcspeaker_beep(523u, 50u);
    pcspeaker_beep(659u, 50u);
    pcspeaker_beep(784u, 60u);
    pcspeaker_beep(1046u, 140u);
}

void pcspeaker_play_success_chime(void) {
    /* Grand triumphant sequence (E5 -> G5 -> C6 -> E6) */
    pcspeaker_beep(659u, 60u);
    pcspeaker_beep(784u, 60u);
    pcspeaker_beep(1046u, 80u);
    pcspeaker_beep(1318u, 180u);
}
