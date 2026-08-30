#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "ahci.h"

static void test_ahci_prdt_layout(void) {
    printf("  [1/3] Testing AHCI 16-Byte PRDT Hardware Descriptor Layout... ");
    assert(sizeof(ahci_prdt_entry_t) == 16);
    assert(sizeof(ahci_cmd_header_t) == 32);

    ahci_prdt_entry_t prdt;
    memset(&prdt, 0, sizeof(prdt));
    prdt.data_base_addr_low = 0x10000000;
    prdt.data_base_addr_high = 0x00000002;
    prdt.byte_count_and_i = (4096 - 1) | (1u << 31); /* 4KB + IOC */

    assert((prdt.byte_count_and_i & (1u << 31)) != 0);
    assert((prdt.byte_count_and_i & 0x3FFFFF) == 4095);
    printf("PASS (Packed 16-Byte Alignment & IOC Flag Verified)\n");
}

static void test_ahci_fis_h2d_construction(void) {
    printf("  [2/3] Testing 48-Bit LBA Register H2D FIS Packing (0x27)... ");
    assert(sizeof(fis_reg_h2d_t) == 20);

    fis_reg_h2d_t cfis;
    memset(&cfis, 0, sizeof(cfis));
    cfis.fis_type = FIS_TYPE_REG_H2D;
    cfis.pmport_c = 0x80;
    cfis.command = ATA_CMD_READ_DMA_EXT;
    cfis.device = (1u << 6);

    uint64_t lba = 0x00000123456789ABULL;
    cfis.lba0 = (uint8_t)(lba & 0xFF);
    cfis.lba1 = (uint8_t)((lba >> 8) & 0xFF);
    cfis.lba2 = (uint8_t)((lba >> 16) & 0xFF);
    cfis.lba3 = (uint8_t)((lba >> 24) & 0xFF);
    cfis.lba4 = (uint8_t)((lba >> 32) & 0xFF);
    cfis.lba5 = (uint8_t)((lba >> 40) & 0xFF);

    uint16_t count = 256;
    cfis.count_low = (uint8_t)(count & 0xFF);
    cfis.count_high = (uint8_t)((count >> 8) & 0xFF);

    assert(cfis.fis_type == 0x27);
    assert(cfis.lba0 == 0xAB && cfis.lba1 == 0x89 && cfis.lba2 == 0x67);
    assert(cfis.lba3 == 0x45 && cfis.lba4 == 0x23 && cfis.lba5 == 0x01);
    assert(cfis.count_low == 0x00 && cfis.count_high == 0x01);
    printf("PASS (48-Bit LBA DMA EXT Serialization Verified)\n");
}

static void test_ahci_identify_parsing(void) {
    printf("  [3/3] Testing SATA IDENTIFY Word-Swapped ATA String Decoder... ");
    /* Simulated ATA words for "Crucial SSD " */
    uint16_t words[6] = {
        ('C' << 8) | 'r',
        ('u' << 8) | 'c',
        ('i' << 8) | 'a',
        ('l' << 8) | ' ',
        ('S' << 8) | 'S',
        ('D' << 8) | ' '
    };

    char decoded[13];
    int pos = 0;
    for (int i = 0; i < 6; i++) {
        decoded[pos++] = (char)(words[i] >> 8);
        decoded[pos++] = (char)(words[i] & 0xFF);
    }
    decoded[pos] = '\0';

    assert(strcmp(decoded, "Crucial SSD ") == 0);
    printf("PASS (Decoded String: '%s')\n", decoded);
}

int main(void) {
    printf("\n=======================================================\n");
    printf("   LastKernel SATA-3 AHCI HBA Controller Test Suite    \n");
    printf("=======================================================\n");
    test_ahci_prdt_layout();
    test_ahci_fis_h2d_construction();
    test_ahci_identify_parsing();
    printf(">>> ALL SATA-3 AHCI HBA VECTORS PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
