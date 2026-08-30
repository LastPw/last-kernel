#ifndef DRIVERS_AUDIO_HDA_H
#define DRIVERS_AUDIO_HDA_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HDA_PCI_CLASS_MULTIMEDIA 0x04u
#define HDA_PCI_SUBCLASS_HDA     0x03u

/* Intel HDA Memory Registers (MMIO) */
#define HDA_REG_GCAP             0x00u /* Global Capabilities */
#define HDA_REG_VMIN             0x02u /* Minor Version */
#define HDA_REG_VMAJ             0x03u /* Major Version */
#define HDA_REG_OUTPAY           0x04u /* Output Payload Capability */
#define HDA_REG_INPAY            0x06u /* Input Payload Capability */
#define HDA_REG_GCTL             0x08u /* Global Control */
#define HDA_REG_WAKEEN           0x0Cu /* Wake Enable */
#define HDA_REG_STATESTS         0x0Eu /* State Change Status (Codecs present) */
#define HDA_REG_GSTS             0x10u /* Global Status */
#define HDA_REG_INTCTL           0x20u /* Interrupt Control */
#define HDA_REG_INTSTS           0x24u /* Interrupt Status */

#define HDA_REG_CORBLBASE        0x40u /* CORB Lower Base Address */
#define HDA_REG_CORBUBASE        0x44u /* CORB Upper Base Address */
#define HDA_REG_CORBWP           0x48u /* CORB Write Pointer */
#define HDA_REG_CORBRP           0x4Au /* CORB Read Pointer */
#define HDA_REG_CORBCTL          0x4Cu /* CORB Control */
#define HDA_REG_CORBSTS          0x4Du /* CORB Status */
#define HDA_REG_CORBSIZE         0x4Eu /* CORB Size */

#define HDA_REG_RIRBLBASE        0x50u /* RIRB Lower Base Address */
#define HDA_REG_RIRBUBASE        0x54u /* RIRB Upper Base Address */
#define HDA_REG_RIRBWP           0x58u /* RIRB Write Pointer */
#define HDA_REG_RINTCNT          0x5Au /* Response Interrupt Count */
#define HDA_REG_RIRBCTL          0x5Cu /* RIRB Control */
#define HDA_REG_RIRBSTS          0x5Du /* RIRB Status */
#define HDA_REG_RIRBSIZE         0x5Eu /* RIRB Size */

#define HDA_GCTL_CRST            (1u << 0) /* Controller Reset */
#define HDA_CORBCTL_RUN          (1u << 1) /* Enable CORB DMA Engine */
#define HDA_RIRBCTL_RUN          (1u << 1) /* Enable RIRB DMA Engine */

#define HDA_MAX_BDL_ENTRIES      32u
#define HDA_MAX_CORB_ENTRIES     256u
#define HDA_MAX_RIRB_ENTRIES     256u

/* 16-Byte Packed Buffer Descriptor List (BDL) Entry */
typedef struct {
    u32 addr_low;             /* Physical Buffer Address Low */
    u32 addr_high;            /* Physical Buffer Address High */
    u32 length;               /* Buffer Length in Bytes */
    u32 flags;                /* Bit 0: Interrupt on Completion (IOC) */
} __attribute__((packed)) hda_bdl_entry_t;

/* Stream Descriptor Registers (32 Bytes per stream) */
typedef volatile struct {
    u8  ctl_low;              /* Stream Run, Interrupt Enable */
    u8  ctl_mid;
    u8  ctl_high;             /* Stream Number */
    u8  sts;                  /* Stream Status */
    u32 lpib;                 /* Link Position in Buffer */
    u32 cbl;                  /* Cyclic Buffer Length */
    u16 lvi;                  /* Last Valid Index */
    u16 fifod;                /* FIFO Size */
    u16 fmt;                  /* Sample Format */
    u16 reserved;
    u32 bdpl;                 /* BDL Base Address Low */
    u32 bdpu;                 /* BDL Base Address High */
} __attribute__((packed)) hda_stream_desc_t;

/* In-Memory HDA Controller State */
typedef struct {
    u8   is_initialized;
    u8   codec_mask;          /* Bitmask of discovered codecs (0..15) */
    u32  sample_rate_hz;      /* 44100, 48000, 96000, 192000 */
    u8   channels;            /* 2 (Stereo), 6 (5.1 Surround), 8 (7.1) */
    u8   bits_per_sample;     /* 16, 24, 32 */
    char codec_vendor[32];
    char codec_name[32];
} hda_controller_t;

/* Public API */
int  hda_init(void);
int  hda_available(void);
int  hda_set_volume(u8 volume);
int  hda_play_pcm(const u8* pcm_data, u32 len);
int  hda_beep(u32 freq_hz, u32 duration_ms);
u32  hda_make_verb(u8 cad, u8 nid, u16 verb_id, u8 payload);
int  hda_corb_send_verb(u8 cad, u8 nid, u16 verb_id, u8 payload);
int  hda_play_tone(u32 freq_hz, u32 duration_ms);
int  hda_synthesize_pcm(u32 freq_hz, u32 duration_ms, u16* out_pcm, u32* out_sample_count);
int  hda_set_master_mute(u8 mute_enable);
void hda_get_status(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_AUDIO_HDA_H */
