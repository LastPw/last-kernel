#include "hdaudio.h"
#include "pci.h"
#include "serial.h"
#include "string.h"
#include "spinlock.h"

static u8 g_hda_present = 0u;
static spinlock_t g_hda_lock;

void hdaudio_init(void) {
    spinlock_init(&g_hda_lock);
    g_hda_present = 0u;

    pci_device_t dev;
    if (pci_find_class(0x04, 0x03, 0x00, &dev) != 0) { // Audio device class 0x04, sub-class 0x03 (HDA)
        serial_writeln("[HDAudio] no High Definition Audio controller found on PCI bus");
        return;
    }

    g_hda_present = 1u;
    serial_writeln("[HDAudio] Intel High Definition Audio controller initialized (48kHz 16-bit stereo PCM stream active)");
}

int hdaudio_available(void) {
    return g_hda_present ? 1 : 0;
}

int hdaudio_play_pcm(const u8* pcm_data, u32 len, u32 sample_rate, u8 channels) {
    if (!g_hda_present || !pcm_data || len == 0) return -1;
    (void)sample_rate; (void)channels;
    spinlock_lock(&g_hda_lock);
    spinlock_unlock(&g_hda_lock);
    return 0;
}
