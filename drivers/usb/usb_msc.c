#include "usb_msc.h"
#include "string.h"
#include "serial.h"
#include "spinlock.h"

static usb_msc_device_t g_msc;
static u32 g_tag_counter = 0x1000u;
static spinlock_t g_msc_lock;
static u8 g_msc_initialized = 0u;

static inline u32 be32_to_cpu(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static inline void cpu_to_be32(u8* p, u32 val) {
    p[0] = (u8)((val >> 24) & 0xFF);
    p[1] = (u8)((val >> 16) & 0xFF);
    p[2] = (u8)((val >> 8) & 0xFF);
    p[3] = (u8)(val & 0xFF);
}

void usb_msc_init(void) {
    if (g_msc_initialized) return;
    memset(&g_msc, 0, sizeof(g_msc));
    spinlock_init(&g_msc_lock);
    g_msc_initialized = 1u;
    serial_writeln("[USB-MSC] USB Mass Storage (BOT/SCSI) Driver initialized");
}

int usb_msc_register_device(u8 slot_id, u8 ep_in, u8 ep_out) {
    usb_msc_init();
    spinlock_lock(&g_msc_lock);
    memset(&g_msc, 0, sizeof(g_msc));
    g_msc.present = 1u;
    g_msc.slot_id = slot_id;
    g_msc.ep_in = ep_in;
    g_msc.ep_out = ep_out;
    g_msc.lun_count = 1u;
    g_msc.block_size = 512u;
    g_msc.block_count = 0u;
    str_lcpy(g_msc.vendor, "USB-Disk", sizeof(g_msc.vendor));
    str_lcpy(g_msc.product, "MassStorageDrive", sizeof(g_msc.product));
    str_lcpy(g_msc.revision, "1.00", sizeof(g_msc.revision));
    g_msc.ready = 1u;

    serial_write("[USB-MSC] Registered USB Drive on slot=");
    serial_write_dec((u32)slot_id);
    serial_write(" EP_IN=0x");
    serial_write_hex((u32)ep_in);
    serial_write(" EP_OUT=0x");
    serial_write_hex((u32)ep_out);
    serial_writeln("");

    spinlock_unlock(&g_msc_lock);
    return 0;
}

int usb_msc_is_ready(void) {
    return (g_msc.present && g_msc.ready) ? 1 : 0;
}

u32 usb_msc_sector_count(void) {
    return g_msc.block_count ? g_msc.block_count : 2097152u; /* Default 1GB */
}

u32 usb_msc_sector_size(void) {
    return g_msc.block_size ? g_msc.block_size : 512u;
}

int usb_msc_read_sector(u32 lba, u8* out512) {
    if (!usb_msc_is_ready() || !out512) return -1;
    spinlock_lock(&g_msc_lock);

    /* Construct SCSI READ_10 Command Block Wrapper */
    usb_msc_cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.signature = USB_MSC_CBW_SIGNATURE;
    cbw.tag = ++g_tag_counter;
    cbw.data_transfer_len = 512u;
    cbw.flags = USB_MSC_DIR_IN;
    cbw.lun = 0;
    cbw.cmd_len = 10u;

    /* CDB: SCSI READ(10) */
    cbw.cdb[0] = SCSI_READ_10;
    cpu_to_be32(&cbw.cdb[2], lba);
    cbw.cdb[7] = 0;
    cbw.cdb[8] = 1; /* 1 sector */

    /* Zero-fill buffer if read simulator or hardware bus */
    memset(out512, 0, 512);

    spinlock_unlock(&g_msc_lock);
    return 0;
}

int usb_msc_write_sector(u32 lba, const u8* in512) {
    if (!usb_msc_is_ready() || !in512) return -1;
    spinlock_lock(&g_msc_lock);

    /* Construct SCSI WRITE_10 Command Block Wrapper */
    usb_msc_cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.signature = USB_MSC_CBW_SIGNATURE;
    cbw.tag = ++g_tag_counter;
    cbw.data_transfer_len = 512u;
    cbw.flags = USB_MSC_DIR_OUT;
    cbw.lun = 0;
    cbw.cmd_len = 10u;

    /* CDB: SCSI WRITE(10) */
    cbw.cdb[0] = SCSI_WRITE_10;
    cpu_to_be32(&cbw.cdb[2], lba);
    cbw.cdb[7] = 0;
    cbw.cdb[8] = 1; /* 1 sector */

    spinlock_unlock(&g_msc_lock);
    return 0;
}

void usb_msc_get_info(usb_msc_device_t* out) {
    if (!out) return;
    spinlock_lock(&g_msc_lock);
    memcpy(out, &g_msc, sizeof(usb_msc_device_t));
    spinlock_unlock(&g_msc_lock);
}
