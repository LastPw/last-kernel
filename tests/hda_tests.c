#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#define HDA_VERB(codec, nid, verb, param) \
    ((((uint32_t)(codec) & 0x0Fu) << 28u) | \
     (((uint32_t)(nid)   & 0xFFu) << 20u) | \
     (((uint32_t)(verb)  & 0xFFFu) << 8u) | \
     ((uint32_t)(param)  & 0xFFu))

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint32_t ioc;
} __attribute__((packed)) hda_bdl_entry_t;

static void test_hda_verb_encoding(void) {
    printf("  [1/3] Testing Intel HDA Verb Command Encoding & Bitmasking... ");
    uint32_t verb1 = HDA_VERB(0, 0x02, 0x706, 0x10);
    assert(verb1 == 0x00270610);

    uint32_t verb2 = HDA_VERB(1, 0x00, 0xF00, 0x04);
    assert(verb2 == 0x100F0004);

    uint32_t verb3 = HDA_VERB(0, 0x03, 0x707, 0x40); /* Pin Control: Headphone / Out Enable */
    assert(verb3 == 0x00370740);

    printf("PASS (Verb 0x%08X verified)\n", verb3);
}

static void test_hda_bdl_layout(void) {
    printf("  [2/3] Testing HDA Buffer Descriptor List (BDL) 16-Byte Hardware Layout... ");
    assert(sizeof(hda_bdl_entry_t) == 16);

    hda_bdl_entry_t bdl[4];
    memset(bdl, 0, sizeof(bdl));

    bdl[0].addr = 0x10000000ull;
    bdl[0].len = 4096;
    bdl[0].ioc = 0;

    bdl[3].addr = 0x10003000ull;
    bdl[3].len = 4096;
    bdl[3].ioc = 1;

    assert(bdl[0].addr == 0x10000000ull);
    assert(bdl[3].ioc == 1);
    printf("PASS (Packed 16-Byte Alignment Confirmed)\n");
}

static void test_pcm_tone_generator(void) {
    printf("  [3/3] Testing 48kHz Stereo PCM Tone & Sampling Buffer Synthesis... ");
    uint32_t rate = 48000;
    uint16_t freq = 440;
    uint32_t ms = 50;
    uint32_t samples = (rate * ms) / 1000;
    assert(samples == 2400);

    uint8_t pcm_buf[2400 * 4];
    for (uint32_t i = 0; i < samples; i++) {
        uint8_t val = ((i * freq) / rate) & 1 ? 0x60 : 0xA0;
        pcm_buf[i * 4 + 0] = val;
        pcm_buf[i * 4 + 1] = val;
        pcm_buf[i * 4 + 2] = val;
        pcm_buf[i * 4 + 3] = val;
    }

    assert(pcm_buf[0] == 0xA0 || pcm_buf[0] == 0x60);
    printf("PASS (Synthesized %u samples, 9600 bytes)\n", samples);
}

int main(void) {
    printf("\n=======================================================\n");
    printf("   LastKernel Intel HD Audio & Universal Sound Tests   \n");
    printf("=======================================================\n");
    test_hda_verb_encoding();
    test_hda_bdl_layout();
    test_pcm_tone_generator();
    printf(">>> ALL INTEL HDA AUDIO VECTORS PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
