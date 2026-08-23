#include "disk.h"
#include "driver_bus.h"
#include "heap.h"
#include "io.h"
#include "string.h"
#include "nvme.h"
#include "virtio_blk.h"
#include "usb.h"
#include "usb_msc.h"
#include "page_cache.h"
#include "serial.h"
#include "fde.h"

/* HW-max: the RAM-disk fallback is sized for a modern machine (512 MiB),
   not a 32 MiB embedded carve-out.  The buffer is carved from the heap at
   disk_init time instead of living in .bss, so the kernel image stays
   small enough for Limine to load it off CD-ROM quickly. */
#define DISK_RAM_FALLBACK_BYTES (512u * 1024u * 1024u)
#define DISK_SNAPSHOT_SECTORS 64u
#define DISK_CACHE_ENTRIES 512u

#define ATA_IO_BASE 0x1F0u
#define ATA_CTRL_BASE 0x3F6u

#define ATA_CTRL_SRST 0x04u

#define ATA_REG_DATA      0u
#define ATA_REG_FEATURES  1u
#define ATA_REG_SECCOUNT  2u
#define ATA_REG_LBA0      3u
#define ATA_REG_LBA1      4u
#define ATA_REG_LBA2      5u
#define ATA_REG_HDDEVSEL  6u
#define ATA_REG_COMMAND   7u
#define ATA_REG_STATUS    7u

#define ATA_CMD_READ_SECT  0x20u
#define ATA_CMD_WRITE_SECT 0x30u
#define ATA_CMD_CACHE_FLUSH 0xE7u
#define ATA_CMD_IDENTIFY   0xECu

#define ATA_SR_ERR  0x01u
#define ATA_SR_DRQ  0x08u
#define ATA_SR_DF   0x20u
#define ATA_SR_DRDY 0x40u
#define ATA_SR_BSY  0x80u

#define ATA_POLL_LIMIT 1000000u
#define ATA_IDENTIFY_ATTEMPTS 3u

typedef enum {
    DISK_BACKEND_NONE = 0,
    DISK_BACKEND_RAM = 1,
    DISK_BACKEND_ATA = 2,
    DISK_BACKEND_VIRTIO = 3,
    DISK_BACKEND_USB = 4,
    DISK_BACKEND_NVME = 5
} disk_backend_t;

typedef struct {
    u8 active;
    u32 id;
    u32 sectors;
    u8* data;
    disk_backend_t backend;
    u16 io_base;
    u16 ctrl_base;
    u32 part_start_lba;
    u32 part_total_lba;
    disk_fs_t part_fs;
    disk_info_t info;
} disk_dev_t;

static u8* g_disk_mem = 0;
static u8 g_io_sector[DISK_SECTOR_SIZE];
static u8 g_snap_head[DISK_SNAPSHOT_SECTORS * DISK_SECTOR_SIZE];
static u8 g_snap_tail[DISK_SNAPSHOT_SECTORS * DISK_SECTOR_SIZE];

static u8 g_gpt_entries[32u * DISK_SECTOR_SIZE];
static disk_dev_t g_dev[DISK_MAX_DEVICES];
static u32 g_dev_count = 0u;
static i32 g_cur = -1;
static disk_partition_info_t g_parts[DISK_MAX_DEVICES][DISK_MAX_PARTITIONS];
static int disk_raw_read(disk_dev_t* d, u32 lba, u8* out512);
static int disk_raw_write(disk_dev_t* d, u32 lba, const u8* in512);

/* Physical I/O bypass for FDE layer — avoids recursion since FDE needs
   to read/write the underlying physical sectors without going through
   the transparent FDE hook in disk_read_sector/disk_write_sector. */
static int disk_phys_read(disk_dev_t* d, u32 lba, u8* out512) {
    return disk_raw_read(d, lba, out512);
}
static int disk_phys_write(disk_dev_t* d, u32 lba, const u8* in512) {
    return disk_raw_write(d, lba, in512);
}

typedef struct {
    u8 valid;
    u8 dirty;
    u16 reserved;
    u32 dev_id;
    u32 lba;
    u32 age;
    u8 data[DISK_SECTOR_SIZE];
} disk_cache_entry_t;

static disk_cache_entry_t g_cache[DISK_CACHE_ENTRIES];
static u32 g_cache_age = 1u;

static int drv_disk_probe(void) { return 0; }
static int drv_disk_start(void) { return 0; }
static int drv_disk_stop(void) { return 0; }

static int drv_disk_read(u8* out, u32 len) {
    if (!out || len < DISK_SECTOR_SIZE) return -1;
    if (g_cur < 0 || !g_dev[g_cur].active) return -1;
    memcpy(out, g_io_sector, DISK_SECTOR_SIZE);
    return (int)DISK_SECTOR_SIZE;
}

static int drv_disk_write(const u8* in, u32 len) {
    if (!in || len < DISK_SECTOR_SIZE) return -1;
    memcpy(g_io_sector, in, DISK_SECTOR_SIZE);
    return (int)DISK_SECTOR_SIZE;
}

static int drv_disk_ioctl(u32 cmd, u32 arg) {
    (void)cmd;
    (void)arg;
    return 0;
}

static u32 crc32(const u8* p, u32 n) {
    u32 c = 0xFFFFFFFFu;
    if (!p) return 0u;
    for (u32 i = 0; i < n; i++) {
        c ^= (u32)p[i];
        for (u32 b = 0; b < 8u; b++) {
            u32 mask = (u32)-(i32)(c & 1u);
            c = (c >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~c;
}

static void wr16(u8* p, u16 v) {
    p[0] = (u8)(v & 0xFFu);
    p[1] = (u8)((v >> 8u) & 0xFFu);
}

static void wr32(u8* p, u32 v) {
    p[0] = (u8)(v & 0xFFu);
    p[1] = (u8)((v >> 8u) & 0xFFu);
    p[2] = (u8)((v >> 16u) & 0xFFu);
    p[3] = (u8)((v >> 24u) & 0xFFu);
}

static void wr64(u8* p, u64 v) {
    wr32(p, (u32)(v & 0xFFFFFFFFu));
    wr32(p + 4, (u32)((v >> 32u) & 0xFFFFFFFFu));
}

static u8 fs_to_mbr(disk_fs_t fs) {
    if (fs == DISK_FS_EXT4) return 0x83u;
    if (fs == DISK_FS_NTFS_RO) return 0x07u;
    return 0x7Fu;
}

static void clear_partition_layout(u32 dev) {
    if (dev >= DISK_MAX_DEVICES) return;
    memset(g_parts[dev], 0, sizeof(g_parts[dev]));
}

static void set_partition_layout(u32 dev, u32 index, disk_part_table_t table, disk_fs_t fs,
                                 u32 start, u32 count, u8 bootable) {
    if (dev >= DISK_MAX_DEVICES || index >= DISK_MAX_PARTITIONS) return;
    g_parts[dev][index].valid = 1u;
    g_parts[dev][index].index = (u8)index;
    g_parts[dev][index].bootable = bootable ? 1u : 0u;
    g_parts[dev][index].table = table;
    g_parts[dev][index].fs = fs;
    g_parts[dev][index].start_lba = start;
    g_parts[dev][index].total_lba = count;
}

static void write_mbr_entry(u8* sec, u32 index, u8 type, u32 start, u32 count, u8 bootable) {
    u8* p;
    if (!sec || index >= 4u) return;
    p = sec + 446u + index * 16u;
    memset(p, 0, 16u);
    p[0] = bootable ? 0x80u : 0u;
    p[1] = 0xFFu; p[2] = 0xFFu; p[3] = 0xFFu;
    p[4] = type;
    p[5] = 0xFFu; p[6] = 0xFFu; p[7] = 0xFFu;
    wr32(p + 8u, start);
    wr32(p + 12u, count);
}

static disk_fs_t fs_from_mbr_type(u8 type) {
    if (type == 0x83u) return DISK_FS_EXT4;
    if (type == 0x07u || type == 0x82u || type == 0xEFu) return DISK_FS_NTFS_RO;
    return DISK_FS_LASTFS;
}

static disk_fs_t fs_from_gpt_type(const u8* type) {
    static const u8 linux_guid[16] = { 0xAF,0x3D,0xC6,0x0F,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4 };
    static const u8 ms_guid[16] = { 0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7 };
    if (!type) return DISK_FS_LASTFS;
    if (memcmp(type, linux_guid, 16u) == 0) return DISK_FS_EXT4;
    if (memcmp(type, ms_guid, 16u) == 0) return DISK_FS_NTFS_RO;
    return DISK_FS_LASTFS;
}

static void load_partition_layout(disk_dev_t* d) {
    u8 sec[DISK_SECTOR_SIZE];
    u32 count = 0u;
    if (!d || disk_raw_read(d, 0u, sec) != 0) return;
    clear_partition_layout((u32)g_cur);
    if (sec[510] != 0x55u || sec[511] != 0xAAu) return;
    if (sec[446u + 4u] == 0xEEu) {
        u8 hdr[DISK_SECTOR_SIZE];
        u32 entry_lba, entry_count, entry_size;
        if (disk_raw_read(d, 1u, hdr) != 0 || memcmp(hdr, "EFI PART", 8u) != 0) return;
        entry_lba = (u32)hdr[72] | ((u32)hdr[73] << 8u) | ((u32)hdr[74] << 16u) | ((u32)hdr[75] << 24u);
        entry_count = (u32)hdr[80] | ((u32)hdr[81] << 8u) | ((u32)hdr[82] << 16u) | ((u32)hdr[83] << 24u);
        entry_size = (u32)hdr[84] | ((u32)hdr[85] << 8u) | ((u32)hdr[86] << 16u) | ((u32)hdr[87] << 24u);
        /* Clamp to our max partition capacity */
        if (entry_size < 128u || entry_size > DISK_SECTOR_SIZE) return;
        if (entry_count > DISK_MAX_PARTITIONS) entry_count = DISK_MAX_PARTITIONS;
        for (u32 i = 0u; i < entry_count; i++) {
            u32 byte_off = i * entry_size;
            u32 lba = entry_lba + byte_off / DISK_SECTOR_SIZE;
            u32 off = byte_off % DISK_SECTOR_SIZE;
            if (disk_raw_read(d, lba, sec) != 0 || off + 48u > DISK_SECTOR_SIZE) continue;
            if (sec[off] == 0u) continue;
            u32 start = (u32)sec[off + 32u] | ((u32)sec[off + 33u] << 8u) | ((u32)sec[off + 34u] << 16u) | ((u32)sec[off + 35u] << 24u);
            u32 end = (u32)sec[off + 40u] | ((u32)sec[off + 41u] << 8u) | ((u32)sec[off + 42u] << 16u) | ((u32)sec[off + 43u] << 24u);
            if (end < start) continue;
            set_partition_layout((u32)g_cur, i, DISK_PART_GPT, fs_from_gpt_type(sec + off), start, end - start + 1u, i == 1u);
            count++;
        }
        d->info.last_partition_table = DISK_PART_GPT;
    } else {
        for (u32 i = 0u; i < DISK_MAX_MBR_PARTS; i++) {
            u32 off = 446u + i * 16u;
            u8 type = sec[off + 4u];
            u32 start = (u32)sec[off + 8u] | ((u32)sec[off + 9u] << 8u) | ((u32)sec[off + 10u] << 16u) | ((u32)sec[off + 11u] << 24u);
            u32 len = (u32)sec[off + 12u] | ((u32)sec[off + 13u] << 8u) | ((u32)sec[off + 14u] << 16u) | ((u32)sec[off + 15u] << 24u);
            if (type == 0u || len == 0u) continue;
            set_partition_layout((u32)g_cur, i, DISK_PART_MBR, fs_from_mbr_type(type), start, len, sec[off] == 0x80u);
            count++;
        }
        d->info.last_partition_table = DISK_PART_MBR;
    }
    d->info.active_partitions = (u8)count;
    if (g_parts[(u32)g_cur][2].valid) {
        d->part_start_lba = g_parts[(u32)g_cur][2].start_lba;
        d->part_total_lba = g_parts[(u32)g_cur][2].total_lba;
        d->part_fs = g_parts[(u32)g_cur][2].fs;
    } else if (count != 0u) {
        d->part_start_lba = g_parts[(u32)g_cur][0].start_lba;
        d->part_total_lba = g_parts[(u32)g_cur][0].total_lba;
        d->part_fs = g_parts[(u32)g_cur][0].fs;
    }
}

static void fill_type_guid(disk_fs_t fs, u8 out[16]) {
    static const u8 linux_guid[16] = {
        0xAF,0x3D,0xC6,0x0F,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4
    };
    static const u8 ms_basic_guid[16] = {
        0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7
    };
    static const u8 lastfs_guid[16] = {
        0x4C,0x41,0x53,0x54,0x4F,0x53,0x00,0x01,0x80,0x10,0xAA,0xBB,0xCC,0xDD,0xEE,0x01
    };
    if (fs == DISK_FS_EXT4) memcpy(out, linux_guid, 16u);
    else if (fs == DISK_FS_NTFS_RO) memcpy(out, ms_basic_guid, 16u);
    else memcpy(out, lastfs_guid, 16u);
}

static void write_utf16le_name(u8* dst, const char* name, u32 max_u16) {
    u32 i = 0u;
    while (i < max_u16) {
        u8 ch = name && name[i] ? (u8)name[i] : 0u;
        dst[i * 2u] = ch;
        dst[i * 2u + 1u] = 0u;
        if (ch == 0u) break;
        i++;
    }
}

static void ata_400ns_wait(u16 io_base) {
    u16 control = (io_base == ATA_IO_BASE) ? ATA_CTRL_BASE : (u16)(io_base + 0x206u);
    (void)inb(control);
    (void)inb(control);
    (void)inb(control);
    (void)inb(control);
}

static void ata_reset_channel(u16 io_base) {
    u16 control = (io_base == ATA_IO_BASE) ? ATA_CTRL_BASE : (u16)(io_base + 0x206u);
    outb(control, ATA_CTRL_SRST);
    for (volatile u32 delay = 0u; delay < 100000u; delay++) { }
    outb(control, 0u);
    for (volatile u32 delay = 0u; delay < 100000u; delay++) { }
    ata_400ns_wait(io_base);
}

static int ata_poll(u16 io_base, u8 require_drq) {
    u32 spin = ATA_POLL_LIMIT;
    u8 last = 0u;
    while (spin--) {
        u8 s = inb(io_base + ATA_REG_STATUS);
        last = s;
        if (s & ATA_SR_BSY) continue;
        if (s & ATA_SR_ERR) {
            serial_write("[LastOS] ATA ERR status="); serial_write_dec(s); serial_putc('\n');
            return -1;
        }
        if (s & ATA_SR_DF) {
            serial_write("[LastOS] ATA DF status="); serial_write_dec(s); serial_putc('\n');
            return -1;
        }
        if (require_drq) {
            if (s & ATA_SR_DRQ) return 0;
            continue;
        }
        /* After the 256-word data phase, do not issue a new command while
           the device still advertises a pending data request. */
        if (s & ATA_SR_DRQ) continue;
        return 0;
    }
    serial_write("[LastOS] ATA poll timeout status="); serial_write_dec(last); serial_putc('\n');
    return -1;
}

static int ata_identify_primary(u32* out_sectors) {
    u16 io = ATA_IO_BASE;
    ata_reset_channel(io);
    outb(io + ATA_REG_HDDEVSEL, 0xA0u);
    ata_400ns_wait(io);
    outb(io + ATA_REG_SECCOUNT, 0u);
    outb(io + ATA_REG_LBA0, 0u);
    outb(io + ATA_REG_LBA1, 0u);
    outb(io + ATA_REG_LBA2, 0u);
    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_400ns_wait(io);

    u8 st = inb(io + ATA_REG_STATUS);
    serial_write("[LastOS] ATA identify status=");
    serial_write_dec(st);
    serial_putc('\n');
    if (st == 0u) return -1;
    if (ata_poll(io, 1u) != 0) {
        serial_writeln("[LastOS] ATA identify DRQ timeout");
        return -1;
    }

    u16 id[256];
    for (u32 i = 0; i < 256u; i++) {
        id[i] = inw(io + ATA_REG_DATA);
    }

    if (ata_poll(io, 0u) != 0) {
        serial_writeln("[LastOS] ATA identify completion failed");
        return -1;
    }

    u32 lba28 = ((u32)id[61] << 16) | (u32)id[60];
    serial_write("[LastOS] ATA identify sectors=");
    serial_write_dec(lba28);
    serial_putc('\n');
    if (lba28 == 0u) return -1;
    if (out_sectors) *out_sectors = lba28;
    return 0;
}

static int ata_rw_setup(disk_dev_t* d, u32 lba, u8 cmd) {
    if (!d) return -1;
    if (lba > 0x0FFFFFFFu) return -1;
    /* Do not overwrite a still-busy PIO request with the next command. */
    if (ata_poll(d->io_base, 0u) != 0) {
        serial_write("[LastOS] ATA channel recovery lba="); serial_write_dec(lba); serial_putc('\n');
        ata_reset_channel(d->io_base);
        if (ata_poll(d->io_base, 0u) != 0) return -1;
    }
    outb(d->io_base + ATA_REG_HDDEVSEL, (u8)(0xE0u | ((lba >> 24) & 0x0Fu)));
    ata_400ns_wait(d->io_base);
    outb(d->io_base + ATA_REG_FEATURES, 0u);
    outb(d->io_base + ATA_REG_SECCOUNT, 1u);
    outb(d->io_base + ATA_REG_LBA0, (u8)(lba & 0xFFu));
    outb(d->io_base + ATA_REG_LBA1, (u8)((lba >> 8) & 0xFFu));
    outb(d->io_base + ATA_REG_LBA2, (u8)((lba >> 16) & 0xFFu));
    outb(d->io_base + ATA_REG_COMMAND, cmd);
    return 0;
}

static int ata_read_sector(disk_dev_t* d, u32 lba, u8* out512) {
    if (!d || !out512) return -1;
    if (ata_rw_setup(d, lba, ATA_CMD_READ_SECT) != 0) return -1;
    if (ata_poll(d->io_base, 1u) != 0) return -1;
    for (u32 i = 0; i < 256u; i++) {
        u16 w = inw(d->io_base + ATA_REG_DATA);
        out512[i * 2u] = (u8)(w & 0xFFu);
        out512[i * 2u + 1u] = (u8)((w >> 8u) & 0xFFu);
    }
    /* The data phase can finish before the device clears BSY. */
    return ata_poll(d->io_base, 0u);
}

static int ata_write_sector_raw(disk_dev_t* d, u32 lba, const u8* in512) {
    if (!d || !in512) return -1;
    if (ata_rw_setup(d, lba, ATA_CMD_WRITE_SECT) != 0) return -1;
    if (ata_poll(d->io_base, 1u) != 0) {
        serial_write("[LastOS] ATA write DRQ failed lba="); serial_write_dec(lba); serial_putc('\n');
        return -1;
    }
    for (u32 i = 0; i < 256u; i++) {
        u16 w = (u16)in512[i * 2u] | ((u16)in512[i * 2u + 1u] << 8u);
        outw(d->io_base + ATA_REG_DATA, w);
    }
    /* The device may still be busy after the final PIO word.  Fast VFS
       writes issue consecutive sectors, so do not let the next command
       overwrite this request before ATA has accepted it. */
    ata_400ns_wait(d->io_base);
    if (ata_poll(d->io_base, 0u) != 0) {
        /* The data phase has completed; recover the channel and let the
           read-after-write verification decide whether the sector landed. */
        serial_write("[LastOS] ATA write completion recovery lba=");
        serial_write_dec(lba); serial_putc('\n');
        ata_reset_channel(d->io_base);
    }
    return 0;
}

static int ata_flush(disk_dev_t* d) {
    if (!d) return -1;
    outb(d->io_base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    return ata_poll(d->io_base, 0u);
}

static int ata_write_sector(disk_dev_t* d, u32 lba, const u8* in512) {
    /* Completion of WRITE SECTORS is the durable PIO boundary for the
       simple ATA backend.  Some BIOS/QEMU profiles leave CACHE FLUSH busy
       indefinitely even though the sector has been committed and verified. */
    return ata_write_sector_raw(d, lba, in512);
}

static int disk_raw_read(disk_dev_t* d, u32 lba, u8* out512) {
    if (!d || !out512 || lba >= d->sectors) return -1;
    if (d->backend == DISK_BACKEND_NVME) return nvme_read_sector(lba, out512);
    if (d->backend == DISK_BACKEND_VIRTIO) return virtio_blk_read_sector(lba, out512);
    if (d->backend == DISK_BACKEND_USB) {
        if (usb_msc_is_ready()) return usb_msc_read_sector(lba, out512);
        return usb_mass_storage_read_sector(lba, out512);
    }
    if (d->backend == DISK_BACKEND_ATA) return ata_read_sector(d, lba, out512);
    if (!d->data) {
        memset(out512, 0, DISK_SECTOR_SIZE);
        return 0;
    }
    memcpy(out512, d->data + (lba * DISK_SECTOR_SIZE), DISK_SECTOR_SIZE);
    return 0;
}

static int disk_raw_write(disk_dev_t* d, u32 lba, const u8* in512) {
    if (!d || !in512 || lba >= d->sectors || !d->info.writable) return -1;
    if (d->backend == DISK_BACKEND_NVME) return nvme_write_sector(lba, in512);
    if (d->backend == DISK_BACKEND_VIRTIO) return virtio_blk_write_sector(lba, in512);
    if (d->backend == DISK_BACKEND_USB) {
        if (usb_msc_is_ready()) return usb_msc_write_sector(lba, in512);
        return usb_mass_storage_write_sector(lba, in512);
    }
    if (d->backend == DISK_BACKEND_ATA) return ata_write_sector(d, lba, in512);
    if (!d->data) return -1;
    memcpy(d->data + (lba * DISK_SECTOR_SIZE), in512, DISK_SECTOR_SIZE);
    return 0;
}

void disk_init(void) {
    driver_ops_t ops;
    memset(g_io_sector, 0, sizeof(g_io_sector));
    memset(g_snap_head, 0, sizeof(g_snap_head));
    memset(g_snap_tail, 0, sizeof(g_snap_tail));
    memset(g_dev, 0, sizeof(g_dev));
    memset(g_parts, 0, sizeof(g_parts));
    memset(g_cache, 0, sizeof(g_cache));
    g_cache_age = 1u;

    g_dev[0].active = 1u;
    g_dev[0].id = 0u;
    g_dev[0].io_base = ATA_IO_BASE;
    g_dev[0].ctrl_base = ATA_CTRL_BASE;
    g_dev[0].info.present = 1u;
    g_dev[0].info.writable = 1u;
    g_dev[0].info.bootable = 1u;
    g_dev[0].info.id = 0u;
    g_dev[0].info.sector_size = DISK_SECTOR_SIZE;
    g_dev[0].info.health_score = 100u;
    g_dev[0].info.last_partition_table = DISK_PART_MBR;
    g_dev[0].part_start_lba = 0u;
    g_dev[0].part_total_lba = 0u;
    g_dev[0].part_fs = DISK_FS_LASTFS;

    /* Probe order: NVMe, then virtio, then ATA, then a RAM disk as the last
       resort. NVMe goes first because a machine that has one is booting from
       it; the ATA path below would otherwise pick up a legacy/emulated
       controller and hand back the wrong device. */
    u32 nvme_sectors = 0u;
    u32 virtio_sectors = 0u;
    if (nvme_init() == 0 && nvme_ready() && nvme_sector_count() > 8192u) {
        nvme_sectors = nvme_sector_count();
        g_dev[0].backend = DISK_BACKEND_NVME;
        g_dev[0].sectors = nvme_sectors;
        g_dev[0].data = 0;
        g_dev[0].info.persistent = 1u;
    } else if (virtio_blk_init() == 0 && virtio_blk_ready() && virtio_blk_sector_count() > 8192u) {
        virtio_sectors = virtio_blk_sector_count();
        g_dev[0].backend = DISK_BACKEND_VIRTIO;
        g_dev[0].sectors = virtio_sectors;
        g_dev[0].data = 0;
        g_dev[0].info.persistent = 1u;
    } else {
        u32 ata_sectors = 0u;
        int ata_ok = -1;
        for (u32 attempt = 0u; attempt < ATA_IDENTIFY_ATTEMPTS; attempt++) {
            if (ata_identify_primary(&ata_sectors) == 0 && ata_sectors > 8192u) {
                ata_ok = 0;
                break;
            }
            for (volatile u32 delay = 0u; delay < 10000u; delay++) { }
        }
        if (ata_ok == 0) {
            g_dev[0].backend = DISK_BACKEND_ATA;
            g_dev[0].sectors = ata_sectors;
            g_dev[0].data = 0;
            g_dev[0].info.persistent = 1u;
        } else {
            g_dev[0].backend = DISK_BACKEND_RAM;
            if (!g_disk_mem) {
                g_disk_mem = heap_alloc(DISK_RAM_FALLBACK_BYTES);
                if (g_disk_mem) memset(g_disk_mem, 0, DISK_RAM_FALLBACK_BYTES);
            }
            if (g_disk_mem) {
                g_dev[0].sectors = DISK_RAM_FALLBACK_BYTES / DISK_SECTOR_SIZE;
                g_dev[0].data = g_disk_mem;
            } else {
                static u8 g_fallback_ram[4096u * DISK_SECTOR_SIZE];
                g_dev[0].sectors = 4096u; /* tiny static-backed fallback */
                g_dev[0].data = g_fallback_ram;
                serial_writeln("[disk] ram fallback 512MB alloc failed, using static 2MB fallback");
            }
            g_dev[0].info.persistent = 0u;
        }
    }

    g_dev[0].info.total_sectors_low = g_dev[0].sectors;
    g_dev[0].info.total_sectors_high = 0u;
    g_dev[0].info.backend = (u8)g_dev[0].backend;
    g_dev_count = 1u;
    g_cur = 0;
    load_partition_layout(&g_dev[0]);

    /* Keep removable storage as a separate disk so a USB stick cannot
       unexpectedly replace the boot device selected above. */
    if ((usb_msc_is_ready() || usb_mass_storage_ready()) && g_dev_count < DISK_MAX_DEVICES) {
        disk_dev_t* usb = &g_dev[g_dev_count];
        memset(usb, 0, sizeof(*usb));
        usb->active = 1u;
        usb->id = g_dev_count;
        usb->backend = DISK_BACKEND_USB;
        usb->sectors = usb_msc_is_ready() ? usb_msc_sector_count() : usb_mass_storage_sector_count();
        usb->info.present = 1u;
        usb->info.writable = 1u;
        usb->info.id = usb->id;
        usb->info.sector_size = DISK_SECTOR_SIZE;
        usb->info.total_sectors_low = usb->sectors;
        usb->info.health_score = 100u;
        usb->info.persistent = 1u;
        usb->info.backend = (u8)usb->backend;
        usb->part_start_lba = 0u;
        usb->part_total_lba = usb->sectors;
        usb->part_fs = DISK_FS_LASTFS;
        g_dev_count++;
        load_partition_layout(usb);
    }

    ops.probe = drv_disk_probe;
    ops.start = drv_disk_start;
    ops.stop = drv_disk_stop;
    ops.read = drv_disk_read;
    ops.write = drv_disk_write;
    ops.ioctl = drv_disk_ioctl;
    (void)driver_register("disk0", &ops);
    (void)driver_set_flags("disk0", 1u, 1u);
    (void)driver_probe_all();
    (void)driver_start("disk0");
}

u32 disk_count(void) {
    return g_dev_count;
}

int disk_info(u32 index, disk_info_t* out) {
    if (!out || index >= g_dev_count || !g_dev[index].active) return -1;
    *out = g_dev[index].info;
    return 0;
}

int disk_select(u32 index) {
    if (index >= g_dev_count || !g_dev[index].active) return -1;
    g_cur = (i32)index;
    return 0;
}

static int cache_find(u32 dev_id, u32 lba) {
    for (u32 i = 0u; i < DISK_CACHE_ENTRIES; i++) {
        if (g_cache[i].valid && g_cache[i].dev_id == dev_id && g_cache[i].lba == lba) return (int)i;
    }
    return -1;
}

static int cache_victim(void) {
    u32 oldest = 0xFFFFFFFFu;
    int victim = 0;
    for (u32 i = 0u; i < DISK_CACHE_ENTRIES; i++) {
        if (!g_cache[i].valid) return (int)i;
        if (g_cache[i].age < oldest) {
            oldest = g_cache[i].age;
            victim = (int)i;
        }
    }
    return victim;
}

static void cache_touch(disk_cache_entry_t* e) {
    if (!e) return;
    e->age = g_cache_age++;
    if (g_cache_age == 0u) g_cache_age = 1u;
}

static int disk_page_cache_raw_read(u32 dev_id, u32 lba, u8* out512) {
    if (g_cur < 0 || (u32)g_cur >= g_dev_count) return -1;
    (void)dev_id;
    return disk_raw_read(&g_dev[g_cur], lba, out512);
}

static int disk_page_cache_raw_write(u32 dev_id, u32 lba, const u8* in512) {
    if (g_cur < 0 || (u32)g_cur >= g_dev_count) return -1;
    (void)dev_id;
    return disk_raw_write(&g_dev[g_cur], lba, in512);
}

int disk_read_sector(u32 lba, u8* out512) {
    if (g_cur < 0 || !out512) return -1;

    /* Transparent FDE: if encryption is active for this device, map absolute LBA
       to FDE-relative LBA and decrypt on read */
    if (fde_is_active()) {
        u32 enc_start, enc_count;
        fde_encrypted_range(&enc_start, &enc_count);
        if (enc_count > 0 && lba >= enc_start && lba < enc_start + enc_count) {
            return fde_read_sector(lba - enc_start, out512);
        }
        /* LBA falls outside encrypted range — pass through unmodified */
    }

    return page_cache_read_sector(g_dev[g_cur].id, lba, out512, disk_page_cache_raw_read);
}

/* Physical read — bypasses FDE, reads raw underlying storage */
int disk_read_sector_physical(u32 lba, u8* out512) {
    if (g_cur < 0 || !out512) return -1;
    return disk_phys_read(&g_dev[g_cur], lba, out512);
}

int disk_write_sector(u32 lba, const u8* in512) {
    if (g_cur < 0 || !in512) return -1;

    /* Transparent FDE: if encryption is active, map absolute LBA
       to FDE-relative LBA and encrypt on write */
    if (fde_is_active()) {
        u32 enc_start, enc_count;
        fde_encrypted_range(&enc_start, &enc_count);
        if (enc_count > 0 && lba >= enc_start && lba < enc_start + enc_count) {
            return fde_write_sector(lba - enc_start, in512);
        }
        /* LBA falls outside encrypted range — pass through unmodified */
    }

    return page_cache_write_sector(g_dev[g_cur].id, lba, in512, disk_page_cache_raw_write);
}

/* Physical write — bypasses FDE, writes raw underlying storage */
int disk_write_sector_physical(u32 lba, const u8* in512) {
    if (g_cur < 0 || !in512) return -1;
    return disk_phys_write(&g_dev[g_cur], lba, in512);
}

int disk_write_sector_fast(u32 lba, const u8* in512) {
    if (g_cur < 0 || !in512) return -1;
    disk_dev_t* d = &g_dev[g_cur];

    /* FDE: encrypt if LBA falls within encrypted range */
    if (fde_is_active()) {
        u32 enc_start, enc_count;
        fde_encrypted_range(&enc_start, &enc_count);
        if (enc_count > 0 && lba >= enc_start && lba < enc_start + enc_count) {
            return fde_write_sector(lba - enc_start, in512);
        }
    }
    if (d->backend == DISK_BACKEND_ATA) {
        if (ata_write_sector_raw(d, lba, in512) != 0) return -1;
    } else if (disk_raw_write(d, lba, in512) != 0) {
        return -1;
    }
    int slot = cache_find(d->id, lba);
    if (slot < 0) slot = cache_victim();
    g_cache[(u32)slot].valid = 1u;
    g_cache[(u32)slot].dirty = 0u;
    g_cache[(u32)slot].dev_id = d->id;
    g_cache[(u32)slot].lba = lba;
    memcpy(g_cache[(u32)slot].data, in512, DISK_SECTOR_SIZE);
    cache_touch(&g_cache[(u32)slot]);
    return 0;
}

int disk_flush(void) {
    if (g_cur < 0) return -1;
    disk_dev_t* d = &g_dev[g_cur];
    page_cache_sync(disk_page_cache_raw_write);
    if (d->backend == DISK_BACKEND_ATA) return ata_flush(d);
    return 0;
}

int disk_copy_sectors(u32 src_lba, u32 dst_lba, u32 count) {
    u8 sec[DISK_SECTOR_SIZE];
    if (g_cur < 0) return -1;
    disk_dev_t* d = &g_dev[g_cur];
    if (src_lba >= d->sectors || dst_lba >= d->sectors) return -1;
    if (count > d->sectors - src_lba) return -1;
    if (count > d->sectors - dst_lba) return -1;
    for (u32 i = 0; i < count; i++) {
        if (disk_raw_read(d, src_lba + i, sec) != 0) return -1;
        if (disk_raw_write(d, dst_lba + i, sec) != 0) return -1;
    }
    return 0;
}

u32 disk_checksum_range(u32 start_lba, u32 count) {
    u8 sec[DISK_SECTOR_SIZE];
    u32 crc = 0xFFFFFFFFu;
    if (g_cur < 0) return 0u;
    disk_dev_t* d = &g_dev[g_cur];
    if (start_lba >= d->sectors) return 0u;
    if (count > d->sectors - start_lba) count = d->sectors - start_lba;
    for (u32 i = 0; i < count; i++) {
        if (disk_raw_read(d, start_lba + i, sec) != 0) break;
        crc ^= crc32(sec, DISK_SECTOR_SIZE);
    }
    return crc;
}

int disk_snapshot_begin(void) {
    if (g_cur < 0) return -1;
    disk_dev_t* d = &g_dev[g_cur];
    if (d->sectors < (DISK_SNAPSHOT_SECTORS * 2u + 2u)) return -1;
    for (u32 i = 0; i < DISK_SNAPSHOT_SECTORS; i++) {
        if (disk_raw_read(d, i, g_snap_head + (i * DISK_SECTOR_SIZE)) != 0) {
            return -1;
        }
        if (disk_raw_read(d, (d->sectors - DISK_SNAPSHOT_SECTORS) + i, g_snap_tail + (i * DISK_SECTOR_SIZE)) != 0) {
            return -1;
        }
    }
    d->info.rollback_ready = 1u;
    return 0;
}

void disk_snapshot_discard(void) {
    if (g_cur < 0) return;
    g_dev[g_cur].info.rollback_ready = 0u;
}

int disk_snapshot_rollback(void) {
    if (g_cur < 0) return -1;
    disk_dev_t* d = &g_dev[g_cur];
    if (!d->info.rollback_ready) return -1;
    for (u32 i = 0; i < DISK_SNAPSHOT_SECTORS; i++) {
        if (disk_raw_write(d, i, g_snap_head + (i * DISK_SECTOR_SIZE)) != 0) return -1;
        if (disk_raw_write(d, (d->sectors - DISK_SNAPSHOT_SECTORS) + i, g_snap_tail + (i * DISK_SECTOR_SIZE)) != 0) return -1;
    }
    d->info.rollback_ready = 0u;
    d->info.last_checksum = disk_checksum_range(0u, 128u);
    return 0;
}

int disk_detect_other_os(void) {
    u8 sec[DISK_SECTOR_SIZE];
    if (disk_read_sector(0u, sec) != 0) return 0;
    if (sec[510] != 0x55u || sec[511] != 0xAAu) return 0;
    for (u32 i = 0; i < 4u; i++) {
        u8* pe = sec + 446u + (i * 16u);
        u8 type = pe[4];
        if (type == 0u) continue;
        if (type != 0x7Fu && type != 0xEEu) return 1;
    }
    return 0;
}

static int partition_mbr(disk_fs_t fs, u8 dualboot) {
    u8 sec[DISK_SECTOR_SIZE];
    u32 efi_start = 2048u, efi_len = 4096u;
    u32 boot_start = efi_start + efi_len, boot_len = 32768u;
    u32 swap_len = 8192u;
    u32 root_start = boot_start + boot_len;
    u32 root_len;
    if (g_cur < 0) return -1;
    disk_dev_t* d = &g_dev[g_cur];
    if (d->sectors <= root_start + swap_len + 2048u) return -1;
    root_len = d->sectors - root_start - swap_len - 2048u;

    if (dualboot) {
        if (disk_read_sector(0u, sec) != 0) return -1;
    } else {
        memset(sec, 0, sizeof(sec));
    }

    write_mbr_entry(sec, 0u, 0xEFu, efi_start, efi_len, 0u);
    write_mbr_entry(sec, 1u, 0x83u, boot_start, boot_len, 0x80u);
    write_mbr_entry(sec, 2u, fs_to_mbr(fs), root_start, root_len, 0u);
    write_mbr_entry(sec, 3u, 0x82u, root_start + root_len, swap_len, 0u);
    sec[510] = 0x55u;
    sec[511] = 0xAAu;
    if (disk_write_sector(0u, sec) != 0) {
        return -1;
    }

    d->info.last_partition_table = DISK_PART_MBR;
    d->info.active_partitions = 4u;
    d->info.dualboot_detected = 0u;
    d->info.last_checksum = disk_checksum_range(0u, 8u);
    clear_partition_layout((u32)g_cur);
    set_partition_layout((u32)g_cur, 0u, DISK_PART_MBR, DISK_FS_NTFS_RO, efi_start, efi_len, 0u);
    set_partition_layout((u32)g_cur, 1u, DISK_PART_MBR, DISK_FS_EXT4, boot_start, boot_len, 1u);
    set_partition_layout((u32)g_cur, 2u, DISK_PART_MBR, fs, root_start, root_len, 0u);
    set_partition_layout((u32)g_cur, 3u, DISK_PART_MBR, DISK_FS_NTFS_RO, root_start + root_len, swap_len, 0u);
    d->part_start_lba = root_start;
    d->part_total_lba = root_len;
    d->part_fs = fs;
    return 0;
}

static int partition_gpt(disk_fs_t fs, u8 dualboot) {
    u8 mbr[DISK_SECTOR_SIZE];
    u8 gpt_h[DISK_SECTOR_SIZE];
    u8 gpt_he[DISK_SECTOR_SIZE];
    u8* ents = g_gpt_entries;
    u8 tmp[DISK_SECTOR_SIZE];
    u32 entries_crc;
    u32 hdr_crc;
    u32 first_usable = 34u;
    u32 entry_lba = 2u;
    u32 entry_count = 128u;
    u32 entry_size = 128u;
    u32 ents_sectors = 32u;

    if (g_cur < 0) return -1;
    disk_dev_t* d = &g_dev[g_cur];
    if (d->sectors < 40960u) return -1;

    memset(mbr, 0, sizeof(mbr));
    memset(gpt_h, 0, sizeof(gpt_h));
    memset(gpt_he, 0, sizeof(gpt_he));
    memset(ents, 0, sizeof(g_gpt_entries));

    if (dualboot && disk_read_sector(0u, mbr) != 0) return -1;
    {
        u8* p = mbr + 446u;
        p[0] = 0x00u;
        p[1] = 0xFFu; p[2] = 0xFFu; p[3] = 0xFFu;
        p[4] = 0xEEu;
        p[5] = 0xFFu; p[6] = 0xFFu; p[7] = 0xFFu;
        wr32(p + 8u, 1u);
        wr32(p + 12u, d->sectors - 1u);
        mbr[510] = 0x55u;
        mbr[511] = 0xAAu;
    }

    {
        static const u8 esp_guid[16] = { 0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B };
        static const u8 boot_guid[16] = { 0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7 };
        static const u8 swap_guid[16] = { 0x6D,0xFD,0x57,0x06,0xAB,0xA4,0xC4,0x43,0x84,0xE5,0x09,0x33,0xC8,0x4B,0x4F,0x4F };
        u32 efi_start = 2048u, efi_len = 4096u;
        u32 boot_start = efi_start + efi_len, boot_len = 32768u;
        u32 swap_len = 8192u, root_start = boot_start + boot_len;
        u32 root_len;
        u8 root_guid[16];
        if (d->sectors <= root_start + swap_len + 2048u) return -1;
        root_len = d->sectors - root_start - swap_len - 2048u;
        fill_type_guid(fs, root_guid);
        for (u32 pi = 0u; pi < 4u; pi++) {
            u8* e = ents + pi * entry_size;
            u32 start = 0u, count = 0u;
            const u8* type = 0;
            const char* name = "LastOS";
            if (pi == 0u) { type = esp_guid; start = efi_start; count = efi_len; name = "EFI System"; }
            else if (pi == 1u) { type = boot_guid; start = boot_start; count = boot_len; name = "LastOS Boot"; }
            else if (pi == 2u) { type = root_guid; start = root_start; count = root_len; name = "LastOS Root"; }
            else { type = swap_guid; start = root_start + root_len; count = swap_len; name = "LastOS Swap"; }
            memcpy(e, type, 16u);
            wr32(e + 16u, 0x12345678u + pi); wr16(e + 20u, (u16)(0x90ABu + pi)); wr16(e + 22u, (u16)(0xCDEFu + pi));
            e[24] = 0x80u; e[25] = 0x10u; e[26] = 0x20u; e[27] = (u8)(0x30u + pi);
            e[28] = 0x40u; e[29] = 0x50u; e[30] = 0x60u; e[31] = (u8)(0x70u + pi);
            wr64(e + 32u, (u64)start); wr64(e + 40u, (u64)(start + count - 1u)); wr64(e + 48u, pi == 2u ? 0x4Cu : 0u);
            write_utf16le_name(e + 56u, name, 36u);
        }
    }
    entries_crc = crc32(ents, sizeof(g_gpt_entries));

    memcpy(gpt_h, "EFI PART", 8u);
    wr32(gpt_h + 8u, 0x00010000u);
    wr32(gpt_h + 12u, 92u);
    wr32(gpt_h + 16u, 0u);
    wr32(gpt_h + 20u, 0u);
    wr64(gpt_h + 24u, 1u);
    wr64(gpt_h + 32u, (u64)(d->sectors - 1u));
    wr64(gpt_h + 40u, (u64)first_usable);
    wr64(gpt_h + 48u, (u64)(d->sectors - 34u));
    wr32(gpt_h + 56u, 0xDEADBEEFu);
    wr32(gpt_h + 60u, 0xCAFEBABEu);
    wr32(gpt_h + 64u, 0x11223344u);
    wr32(gpt_h + 68u, 0x55667788u);
    wr64(gpt_h + 72u, (u64)entry_lba);
    wr32(gpt_h + 80u, entry_count);
    wr32(gpt_h + 84u, entry_size);
    wr32(gpt_h + 88u, entries_crc);
    hdr_crc = crc32(gpt_h, 92u);
    wr32(gpt_h + 16u, hdr_crc);

    memcpy(gpt_he, gpt_h, DISK_SECTOR_SIZE);
    wr64(gpt_he + 24u, (u64)(d->sectors - 1u));
    wr64(gpt_he + 32u, 1u);
    wr64(gpt_he + 72u, (u64)(d->sectors - 33u));
    wr32(gpt_he + 16u, 0u);
    hdr_crc = crc32(gpt_he, 92u);
    wr32(gpt_he + 16u, hdr_crc);

    if (disk_write_sector(0u, mbr) != 0) return -1;
    for (u32 i = 0; i < ents_sectors; i++) {
        memcpy(tmp, ents + (i * DISK_SECTOR_SIZE), DISK_SECTOR_SIZE);
        if (disk_write_sector(entry_lba + i, tmp) != 0) return -1;
    }
    if (disk_write_sector(1u, gpt_h) != 0) return -1;
    for (u32 i = 0; i < ents_sectors; i++) {
        memcpy(tmp, ents + (i * DISK_SECTOR_SIZE), DISK_SECTOR_SIZE);
        if (disk_write_sector((d->sectors - 33u) + i, tmp) != 0) return -1;
    }
    if (disk_write_sector(d->sectors - 1u, gpt_he) != 0) return -1;

    d->info.last_partition_table = DISK_PART_GPT;
    d->info.active_partitions = 4u;
    d->info.dualboot_detected = dualboot ? 1u : 0u;
    d->info.last_checksum = disk_checksum_range(0u, 64u);
    clear_partition_layout((u32)g_cur);
    {
        u32 efi_start = 2048u, efi_len = 4096u;
        u32 boot_start = efi_start + efi_len, boot_len = 32768u;
        u32 swap_len = 8192u, root_start = boot_start + boot_len;
        u32 root_len = d->sectors - root_start - swap_len - 2048u;
        set_partition_layout((u32)g_cur, 0u, DISK_PART_GPT, DISK_FS_NTFS_RO, efi_start, efi_len, 0u);
        set_partition_layout((u32)g_cur, 1u, DISK_PART_GPT, DISK_FS_EXT4, boot_start, boot_len, 1u);
        set_partition_layout((u32)g_cur, 2u, DISK_PART_GPT, fs, root_start, root_len, 0u);
        set_partition_layout((u32)g_cur, 3u, DISK_PART_GPT, DISK_FS_NTFS_RO, root_start + root_len, swap_len, 0u);
        d->part_start_lba = root_start;
        d->part_total_lba = root_len;
    }
    d->part_fs = fs;
    return 0;
}

int disk_partition_auto(disk_part_table_t table, disk_fs_t fs, u8 dualboot) {
    if (g_cur < 0) return -1;
    /* Never replace an existing foreign partition map until a real shrink/migrate
       planner has selected free extents. The old one-partition writer was unsafe. */
    if (dualboot && disk_detect_other_os() > 0) return -2;
    if (disk_snapshot_begin() != 0) {
        return -1;
    }
    int rc = (table == DISK_PART_GPT) ? partition_gpt(fs, dualboot) : partition_mbr(fs, dualboot);
    if (rc != 0) {
        (void)disk_snapshot_rollback();
        return -1;
    }
    return 0;
}

int disk_partition_info(disk_partition_info_t* out) {
    if (!out || g_cur < 0) return -1;
    disk_dev_t* d = &g_dev[g_cur];
    out->valid = (d->part_total_lba > 0u) ? 1u : 0u;
    out->table = d->info.last_partition_table;
    out->fs = d->part_fs;
    out->start_lba = d->part_start_lba;
    out->total_lba = d->part_total_lba;
    return 0;
}

u32 disk_partition_count(void) {
    if (g_cur < 0 || (u32)g_cur >= DISK_MAX_DEVICES) return 0u;
    return g_dev[g_cur].info.active_partitions;
}

int disk_partition_get(u32 index, disk_partition_info_t* out) {
    if (!out || g_cur < 0 || (u32)g_cur >= DISK_MAX_DEVICES || index >= DISK_MAX_PARTITIONS) return -1;
    if (!g_parts[(u32)g_cur][index].valid) return -1;
    *out = g_parts[(u32)g_cur][index];
    return 0;
}

int disk_free_space(disk_free_space_t* out) {
    disk_dev_t* d;
    u32 cursor;
    u32 used = 0u;
    u32 extents = 0u;
    u32 largest = 0u;
    if (!out || g_cur < 0 || !g_dev[g_cur].active) return -1;
    d = &g_dev[g_cur];
    memset(out, 0, sizeof(*out));
    out->total_sectors = d->sectors;
    cursor = (d->info.last_partition_table == DISK_PART_GPT) ? 34u : 2048u;
    for (u32 n = 0u; n < DISK_MAX_PARTITIONS; n++) {
        disk_partition_info_t* p = &g_parts[(u32)g_cur][n];
        if (p->valid) used += p->total_lba;
    }
    for (;;) {
        u32 next = 0xFFFFFFFFu;
        for (u32 n = 0u; n < DISK_MAX_PARTITIONS; n++) {
            disk_partition_info_t* p = &g_parts[(u32)g_cur][n];
            if (p->valid && p->start_lba >= cursor && p->start_lba < next) next = p->start_lba;
        }
        if (next == 0xFFFFFFFFu) break;
        if (next > cursor) {
            u32 len = next - cursor;
            extents++;
            if (len > largest) largest = len;
        }
        {
            u32 advance = next;
            for (u32 n = 0u; n < DISK_MAX_PARTITIONS; n++) {
                disk_partition_info_t* p = &g_parts[(u32)g_cur][n];
                if (p->valid && p->start_lba == next && p->start_lba + p->total_lba > advance) advance = p->start_lba + p->total_lba;
            }
            if (advance <= cursor) break;
            cursor = advance;
        }
    }
    {
        u32 end = d->sectors;
        if (d->info.last_partition_table == DISK_PART_GPT && end > 33u) end -= 33u;
        if (end > cursor) {
            u32 len = end - cursor;
            extents++;
            if (len > largest) largest = len;
        }
    }
    out->used_sectors = used;
    out->free_sectors = (d->sectors > used) ? d->sectors - used : 0u;
    out->largest_free_extent = largest;
    out->free_extent_count = extents;
    return 0;
}
