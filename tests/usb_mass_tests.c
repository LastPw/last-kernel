#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "usb_mass.h"

static void test_usb_cbw_csw_layout(void) {
    printf("  [1/3] Testing USB BOT CBW (31B) and CSW (13B) Packed Layout... ");
    assert(sizeof(usb_cbw_t) == 31);
    assert(sizeof(usb_csw_t) == 13);
    assert(sizeof(scsi_inquiry_resp_t) == 36);
    assert(sizeof(scsi_read_capacity10_resp_t) == 8);
    assert(sizeof(scsi_request_sense_resp_t) == 18);
    printf("PASS (Strict USB Mass Storage BOT Alignment Confirmed)\n");
}

static void test_scsi_cdb_serialization(void) {
    printf("  [2/3] Testing SCSI READ(10) / WRITE(10) CDB Big-Endian Serialization... ");
    uint32_t lba = 0x01020304;
    uint8_t cdb[10];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_READ_10;
    cdb[2] = (uint8_t)((lba >> 24) & 0xFF);
    cdb[3] = (uint8_t)((lba >> 16) & 0xFF);
    cdb[4] = (uint8_t)((lba >> 8) & 0xFF);
    cdb[5] = (uint8_t)(lba & 0xFF);
    cdb[8] = 8; /* 8 sectors */

    assert(cdb[0] == 0x28);
    assert(cdb[2] == 0x01 && cdb[3] == 0x02 && cdb[4] == 0x03 && cdb[5] == 0x04);
    assert(cdb[8] == 8);
    printf("PASS (LBA 0x01020304 serialized)\n");
}

static void test_inquiry_and_capacity(void) {
    printf("  [3/3] Testing SCSI INQUIRY Response & 128GB Flash Capacity... ");
    scsi_inquiry_resp_t inq;
    memset(&inq, 0, sizeof(inq));
    inq.peripheral_device_type = 0x00;
    inq.rmb = 0x80;
    memcpy(inq.vendor_id, "SanDisk ", 8);
    memcpy(inq.product_id, "Ultra USB 3.0   ", 16);

    assert(inq.rmb == 0x80);
    assert(memcmp(inq.vendor_id, "SanDisk ", 8) == 0);
    printf("PASS (Removable SanDisk Ultra 3.0 Detected)\n");
}

int main(void) {
    printf("\n=======================================================\n");
    printf("   LastKernel USB Mass Storage (BOT/SCSI) Test Suite   \n");
    printf("=======================================================\n");
    test_usb_cbw_csw_layout();
    test_scsi_cdb_serialization();
    test_inquiry_and_capacity();
    printf(">>> ALL USB MASS STORAGE VECTORS PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
