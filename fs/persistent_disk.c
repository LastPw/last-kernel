#include "persistent_disk.h"
#include "disk.h"
#include "string.h"
#include "serial.h"

#define PDISK_SIGNATURE 0x50445256u /* 'PDRV' */
#define PDISK_VERSION 3u
#define PDISK_RESERVED_SECTORS 4096u
#define PDISK_HEADER_SECTOR 0u
#define PDISK_PERSIST_START 1u
#define PDISK_PERSIST_SECTORS 8u
#define PDISK_BLOB_SECTORS 6u
#define PDISK_WAL_START (PDISK_PERSIST_START + PDISK_BLOB_SECTORS)
#define PDISK_PANIC_START (PDISK_WAL_START + 1u)
#define PDISK_VFS_START (PDISK_PERSIST_START + PDISK_PERSIST_SECTORS)
#define PDISK_VFS_SLOT_COUNT 2u
#define PDISK_VFS_SLOT_SECTORS ((PDISK_RESERVED_SECTORS - PDISK_VFS_START) / PDISK_VFS_SLOT_COUNT)
#define PDISK_VFS_DATA_START 1u
#define PDISK_VFS_MAX_DATA_SECTORS (PDISK_VFS_SLOT_SECTORS - PDISK_VFS_DATA_START)
#define PDISK_MAX_BLOB_SIZE (PDISK_BLOB_SECTORS * DISK_SECTOR_SIZE)
#define PDISK_MAX_VFS_BLOB_SIZE (PDISK_VFS_MAX_DATA_SECTORS * DISK_SECTOR_SIZE)

#define PDISK_VFS_SLOT_MAGIC 0x534C4656u /* 'VFLS' */
#define PDISK_VFS_SLOT_VERSION 1u
#define PDISK_WAL_MAGIC 0x4C415757u /* 'WAWL' */
#define PDISK_WAL_VERSION 1u
#define PDISK_WAL_PREPARED 1u
#define PDISK_WAL_COMMITTED 2u

typedef struct {
    u32 magic;
    u32 version;
    u32 state;
    u32 generation;
    u32 slot;
    u32 length;
    u32 crc;
    u32 checksum;
    u8 reserved[DISK_SECTOR_SIZE - 32u];
} pdisk_wal_record_t;

typedef struct {
    u32 signature;
    u32 version;
    u32 reserved_sectors;
    u32 blob_length;
    u32 blob_crc;
    u32 vfs_length;
    u32 vfs_crc;
    u32 vfs_generation;
    u32 vfs_slot;
    u32 checksum;
    u8 reserved[DISK_SECTOR_SIZE - 40u];
} pdisk_header_t;

typedef struct {
    u32 magic;
    u32 version;
    u32 generation;
    u32 length;
    u32 crc;
    u32 checksum;
    u8 reserved[DISK_SECTOR_SIZE - 24u];
} pdisk_vfs_slot_header_t;

typedef struct {
    u32 signature;
    u32 version;
    u32 reserved_sectors;
    u32 blob_length;
    u32 blob_crc;
    u32 vfs_length;
    u32 vfs_crc;
    u32 checksum;
    u8 reserved[DISK_SECTOR_SIZE - 32u];
} pdisk_legacy_header_t;

static u32 g_reserved_start = 0u;
static u32 g_reserved_sectors = 0u;
static u8 g_ready = 0u;

static u32 pdisk_crc32(const u8* data, u32 length) {
    u32 crc = 0xFFFFFFFFu;
    if (!data) return 0u;
    for (u32 i = 0u; i < length; i++) {
        crc ^= data[i];
        for (u8 j = 0u; j < 8u; j++) {
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
        }
    }
    return ~crc;
}

static u32 pdisk_header_checksum(const pdisk_header_t* hdr) {
    pdisk_header_t tmp;
    if (!hdr) return 0u;
    memcpy(&tmp, hdr, sizeof(tmp));
    tmp.checksum = 0u;
    return pdisk_crc32((const u8*)&tmp, sizeof(tmp));
}

static int pdisk_read_sector(u32 lba, u8* buffer) {
    return disk_read_sector(g_reserved_start + lba, buffer);
}

static int pdisk_write_sector(u32 lba, const u8* buffer) {
    return disk_write_sector(g_reserved_start + lba, buffer);
}

static int pdisk_write_sector_fast(u32 lba, const u8* buffer) {
    return disk_write_sector_fast(g_reserved_start + lba, buffer);
}

static int pdisk_write_header(pdisk_header_t* hdr);
static int pdisk_read_header(pdisk_header_t* hdr);
static int pdisk_read_vfs_slot_header(u32 slot, pdisk_vfs_slot_header_t* hdr);
static int pdisk_load_vfs_slot(u32 slot, const pdisk_vfs_slot_header_t* hdr, void* out, u32 out_size);

static u32 pdisk_vfs_slot_start(u32 slot) {
    return PDISK_VFS_START + (slot * PDISK_VFS_SLOT_SECTORS);
}

static u32 pdisk_vfs_slot_checksum(const pdisk_vfs_slot_header_t* hdr) {
    pdisk_vfs_slot_header_t tmp;
    if (!hdr) return 0u;
    memcpy(&tmp, hdr, sizeof(tmp));
    tmp.checksum = 0u;
    return pdisk_crc32((const u8*)&tmp, sizeof(tmp));
}

static u32 pdisk_wal_checksum(const pdisk_wal_record_t* record) {
    pdisk_wal_record_t tmp;
    if (!record) return 0u;
    memcpy(&tmp, record, sizeof(tmp));
    tmp.checksum = 0u;
    return pdisk_crc32((const u8*)&tmp, sizeof(tmp));
}

static int pdisk_wal_read(pdisk_wal_record_t* record) {
    u8 sector[DISK_SECTOR_SIZE];
    if (!record || pdisk_read_sector(PDISK_WAL_START, sector) != 0) return -1;
    memcpy(record, sector, sizeof(*record));
    if (record->magic != PDISK_WAL_MAGIC || record->version != PDISK_WAL_VERSION ||
        record->state < PDISK_WAL_PREPARED || record->state > PDISK_WAL_COMMITTED ||
        record->slot >= PDISK_VFS_SLOT_COUNT || record->length > PDISK_MAX_VFS_BLOB_SIZE ||
        record->checksum != pdisk_wal_checksum(record)) return -1;
    return 0;
}

static int pdisk_wal_write(const pdisk_wal_record_t* record) {
    u8 sector[DISK_SECTOR_SIZE];
    if (!record) return -1;
    memset(sector, 0, sizeof(sector));
    memcpy(sector, record, sizeof(*record));
    return pdisk_write_sector(PDISK_WAL_START, sector);
}

int pdisk_wal_clear(void) {
    u8 sector[DISK_SECTOR_SIZE];
    if (!g_ready) return -1;
    memset(sector, 0, sizeof(sector));
    return pdisk_write_sector(PDISK_WAL_START, sector);
}

int pdisk_test_torn_vfs_wal(void) {
    pdisk_header_t hdr;
    pdisk_vfs_slot_header_t slots[PDISK_VFS_SLOT_COUNT];
    u8 valid[PDISK_VFS_SLOT_COUNT];
    u32 active = 0u, generation = 0u;
    if (!g_ready || pdisk_read_header(&hdr) != 0) return -1;
    memset(slots, 0, sizeof(slots));
    memset(valid, 0, sizeof(valid));
    for (u32 i = 0u; i < PDISK_VFS_SLOT_COUNT; i++) {
        if (pdisk_read_vfs_slot_header(i, &slots[i]) == 0 && slots[i].length != 0u) {
            valid[i] = 1u;
            if (slots[i].generation >= generation) {
                generation = slots[i].generation;
                active = i;
            }
        }
    }
    if (!valid[0] && !valid[1]) return -1;
    /* Leave a valid PREPARED record without writing its target slot. */
    return pdisk_wal_begin(generation + 1u, active ^ 1u,
                           slots[active].length, slots[active].crc);
}

int pdisk_save_panic(const void* record, u32 len) {
    u8 sector[DISK_SECTOR_SIZE];
    if (!g_ready || !record || len == 0u || len > DISK_SECTOR_SIZE) return -1;
    memset(sector, 0, sizeof(sector));
    memcpy(sector, record, len);
    return pdisk_write_sector(PDISK_PANIC_START, sector);
}

int pdisk_load_panic(void* record, u32 len) {
    u8 sector[DISK_SECTOR_SIZE];
    if (!g_ready || !record || len == 0u || len > DISK_SECTOR_SIZE) return -1;
    if (pdisk_read_sector(PDISK_PANIC_START, sector) != 0) return -1;
    memcpy(record, sector, len);
    return 0;
}

int pdisk_clear_panic(void) {
    u8 sector[DISK_SECTOR_SIZE];
    if (!g_ready) return -1;
    memset(sector, 0, sizeof(sector));
    return pdisk_write_sector(PDISK_PANIC_START, sector);
}

int pdisk_wal_begin(u32 generation, u32 slot, u32 length, u32 crc) {
    pdisk_wal_record_t record;
    if (!g_ready || slot >= PDISK_VFS_SLOT_COUNT || length > PDISK_MAX_VFS_BLOB_SIZE) return -1;
    memset(&record, 0, sizeof(record));
    record.magic = PDISK_WAL_MAGIC;
    record.version = PDISK_WAL_VERSION;
    record.state = PDISK_WAL_PREPARED;
    record.generation = generation;
    record.slot = slot;
    record.length = length;
    record.crc = crc;
    record.checksum = pdisk_wal_checksum(&record);
    return pdisk_wal_write(&record);
}

int pdisk_wal_commit(u32 generation) {
    pdisk_wal_record_t record;
    if (pdisk_wal_read(&record) != 0 || record.generation != generation) return -1;
    record.state = PDISK_WAL_COMMITTED;
    record.checksum = pdisk_wal_checksum(&record);
    return pdisk_wal_write(&record);
}

int pdisk_wal_recover(void) {
    pdisk_wal_record_t record;
    if (!g_ready || pdisk_wal_read(&record) != 0) return 0;
    pdisk_vfs_slot_header_t slot;
    u8 valid_slot = 0u;
    if (pdisk_read_vfs_slot_header(record.slot, &slot) == 0 &&
        slot.generation == record.generation && slot.length == record.length && slot.crc == record.crc) {
        static u8 verify[PDISK_MAX_VFS_BLOB_SIZE];
        if (pdisk_load_vfs_slot(record.slot, &slot, verify, sizeof(verify)) == 0) valid_slot = 1u;
    }
    if (valid_slot && (record.state == PDISK_WAL_PREPARED || record.state == PDISK_WAL_COMMITTED)) {
        pdisk_header_t hdr;
        if (pdisk_read_header(&hdr) != 0) memset(&hdr, 0, sizeof(hdr));
        hdr.signature = PDISK_SIGNATURE;
        hdr.version = PDISK_VERSION;
        hdr.reserved_sectors = PDISK_RESERVED_SECTORS;
        hdr.vfs_length = slot.length;
        hdr.vfs_crc = slot.crc;
        hdr.vfs_generation = slot.generation;
        hdr.vfs_slot = record.slot;
        hdr.checksum = pdisk_header_checksum(&hdr);
        if (pdisk_write_header(&hdr) != 0) return -1;
    }
    /* Incomplete writes are discarded; a verified slot is now the active snapshot. */
    return pdisk_wal_clear();
}

static int pdisk_read_vfs_slot_header(u32 slot, pdisk_vfs_slot_header_t* hdr) {
    u8 sector[DISK_SECTOR_SIZE];
    if (!hdr || slot >= PDISK_VFS_SLOT_COUNT) return -1;
    if (pdisk_read_sector(pdisk_vfs_slot_start(slot), sector) != 0) return -1;
    memcpy(hdr, sector, sizeof(*hdr));
    if (hdr->magic != PDISK_VFS_SLOT_MAGIC || hdr->version != PDISK_VFS_SLOT_VERSION ||
        hdr->length > PDISK_MAX_VFS_BLOB_SIZE || hdr->checksum != pdisk_vfs_slot_checksum(hdr)) {
        return -1;
    }
    return 0;
}

static int pdisk_load_vfs_slot(u32 slot, const pdisk_vfs_slot_header_t* hdr,
                               void* out, u32 out_size) {
    if (!hdr || !out || hdr->length == 0u || out_size < hdr->length) return -1;
    u32 sectors = (hdr->length + DISK_SECTOR_SIZE - 1u) / DISK_SECTOR_SIZE;
    if (sectors > PDISK_VFS_MAX_DATA_SECTORS) return -1;
    for (u32 i = 0u; i < sectors; i++) {
        u8 sector[DISK_SECTOR_SIZE];
        u32 offset = i * DISK_SECTOR_SIZE;
        u32 chunk = hdr->length - offset;
        if (chunk > DISK_SECTOR_SIZE) chunk = DISK_SECTOR_SIZE;
        if (pdisk_read_sector(pdisk_vfs_slot_start(slot) + PDISK_VFS_DATA_START + i, sector) != 0) return -1;
        memcpy(((u8*)out) + offset, sector, chunk);
    }
    return pdisk_crc32((const u8*)out, hdr->length) == hdr->crc ? 0 : -1;
}

static u32 pdisk_legacy_header_checksum(const pdisk_legacy_header_t* hdr) {
    pdisk_legacy_header_t tmp;
    if (!hdr) return 0u;
    memcpy(&tmp, hdr, sizeof(tmp));
    tmp.checksum = 0u;
    return pdisk_crc32((const u8*)&tmp, sizeof(tmp));
}

static int pdisk_migrate_legacy(const pdisk_legacy_header_t* old) {
    if (!old || old->vfs_length > (512u - PDISK_VFS_START) * DISK_SECTOR_SIZE ||
        old->vfs_length > PDISK_MAX_VFS_BLOB_SIZE) return -1;

    u32 sectors = (old->vfs_length + DISK_SECTOR_SIZE - 1u) / DISK_SECTOR_SIZE;
    u32 crc = 0xFFFFFFFFu;
    u8 sector[DISK_SECTOR_SIZE];
    for (u32 i = 0u; i < sectors; i++) {
        if (disk_read_sector(PDISK_VFS_START + i, sector) != 0) return -1;
        u32 chunk = old->vfs_length - (i * DISK_SECTOR_SIZE);
        if (chunk > DISK_SECTOR_SIZE) chunk = DISK_SECTOR_SIZE;
        for (u32 j = 0u; j < chunk; j++) {
            crc ^= sector[j];
            for (u8 b = 0u; b < 8u; b++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
        }
    }
    if ((~crc) != old->vfs_crc) return -1;

    /* Source and destination overlap by one sector, so copy backwards. */
    for (u32 i = sectors; i > 0u; i--) {
        u32 index = i - 1u;
        if (disk_read_sector(PDISK_VFS_START + index, sector) != 0 ||
            pdisk_write_sector(pdisk_vfs_slot_start(0u) + PDISK_VFS_DATA_START + index, sector) != 0) {
            return -1;
        }
    }

    pdisk_vfs_slot_header_t slot;
    memset(&slot, 0, sizeof(slot));
    slot.magic = PDISK_VFS_SLOT_MAGIC;
    slot.version = PDISK_VFS_SLOT_VERSION;
    slot.generation = 1u;
    slot.length = old->vfs_length;
    slot.crc = old->vfs_crc;
    slot.checksum = pdisk_vfs_slot_checksum(&slot);
    memset(sector, 0, sizeof(sector));
    memcpy(sector, &slot, sizeof(slot));
    if (pdisk_write_sector(pdisk_vfs_slot_start(0u), sector) != 0) return -1;

    pdisk_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.signature = PDISK_SIGNATURE;
    hdr.version = PDISK_VERSION;
    hdr.reserved_sectors = PDISK_RESERVED_SECTORS;
    hdr.blob_length = old->blob_length;
    hdr.blob_crc = old->blob_crc;
    hdr.vfs_length = slot.length;
    hdr.vfs_crc = slot.crc;
    hdr.vfs_generation = slot.generation;
    hdr.vfs_slot = 0u;
    hdr.checksum = pdisk_header_checksum(&hdr);
    return pdisk_write_header(&hdr);
}

static int pdisk_read_header(pdisk_header_t* hdr) {
    u8 sector[DISK_SECTOR_SIZE];
    if (!hdr) return -1;
    if (pdisk_read_sector(PDISK_HEADER_SECTOR, sector) != 0) return -1;
    memcpy(hdr, sector, sizeof(pdisk_header_t));
    if (hdr->signature != PDISK_SIGNATURE) return -1;
    if (hdr->version != PDISK_VERSION) return -1;
    if (hdr->reserved_sectors != PDISK_RESERVED_SECTORS) return -1;
    u32 expected = pdisk_header_checksum(hdr);
    if (expected != hdr->checksum) return -1;
    if (hdr->blob_length > PDISK_MAX_BLOB_SIZE) return -1;
    if (hdr->vfs_length > PDISK_MAX_VFS_BLOB_SIZE) return -1;
    return 0;
}

static int pdisk_write_header(pdisk_header_t* hdr) {
    u8 sector[DISK_SECTOR_SIZE];
    if (!hdr) return -1;
    memset(sector, 0, sizeof(sector));
    memcpy(sector, hdr, sizeof(*hdr));
    return pdisk_write_sector(PDISK_HEADER_SECTOR, sector);
}

int pdisk_ready(void) {
    return g_ready ? 1 : 0;
}

int pdisk_init(void) {
    disk_info_t info;
    if (disk_count() == 0u) return -1;
    if (disk_info(0u, &info) != 0) return -1;
    if (!info.persistent) return -1;
    if (info.total_sectors_low < (PDISK_RESERVED_SECTORS + 2048u)) return -1;

    (void)disk_select(0u);
    g_reserved_sectors = PDISK_RESERVED_SECTORS;
    g_reserved_start = info.total_sectors_low - g_reserved_sectors;
    g_ready = 1u;
    (void)pdisk_wal_recover();
    g_ready = 0u;

    pdisk_header_t hdr;
    if (pdisk_read_header(&hdr) == 0) {
        g_ready = 1u;
        return 0;
    }

    /* Preserve the previous single-slot layout when upgrading the kernel. */
    {
        u8 raw_header[DISK_SECTOR_SIZE];
        pdisk_legacy_header_t legacy;
        if (disk_read_sector(g_reserved_start, raw_header) == 0) {
            memcpy(&legacy, raw_header, sizeof(legacy));
            if (legacy.signature == PDISK_SIGNATURE && legacy.version == 2u &&
                legacy.reserved_sectors == 512u &&
                legacy.blob_length <= PDISK_MAX_BLOB_SIZE &&
                legacy.vfs_length <= (512u - PDISK_VFS_START) * DISK_SECTOR_SIZE &&
                pdisk_legacy_header_checksum(&legacy) == legacy.checksum) {
                if (pdisk_migrate_legacy(&legacy) == 0) {
                    g_ready = 1u;
                    return 0;
                }
            }
        }
    }

    /* A torn global header must not erase a fully written VFS slot. */
    pdisk_vfs_slot_header_t slots[PDISK_VFS_SLOT_COUNT];
    u8 valid[PDISK_VFS_SLOT_COUNT];
    memset(slots, 0, sizeof(slots));
    memset(valid, 0, sizeof(valid));
    u32 best = 0u;
    for (u32 i = 0u; i < PDISK_VFS_SLOT_COUNT; i++) {
        if (pdisk_read_vfs_slot_header(i, &slots[i]) == 0 && slots[i].length != 0u) {
            valid[i] = 1u;
            if (!valid[best] || slots[i].generation > slots[best].generation) best = i;
        }
    }
    if (valid[0] || valid[1]) {
        memset(&hdr, 0, sizeof(hdr));
        hdr.signature = PDISK_SIGNATURE;
        hdr.version = PDISK_VERSION;
        hdr.reserved_sectors = PDISK_RESERVED_SECTORS;
        hdr.vfs_length = slots[best].length;
        hdr.vfs_crc = slots[best].crc;
        hdr.vfs_generation = slots[best].generation;
        hdr.vfs_slot = best;
        hdr.checksum = pdisk_header_checksum(&hdr);
        if (pdisk_write_header(&hdr) != 0) return -1;
        g_ready = 1u;
        return 0;
    }

    /* A new header makes every old slot unreachable.  Do not synchronously
       clear 1024 sectors here: ATA write+verify makes first boot unusable. */
    memset(&hdr, 0, sizeof(hdr));
    hdr.signature = PDISK_SIGNATURE;
    hdr.version = PDISK_VERSION;
    hdr.reserved_sectors = PDISK_RESERVED_SECTORS;
    hdr.blob_length = 0u;
    hdr.blob_crc = 0u;
    hdr.vfs_length = 0u;
    hdr.vfs_crc = 0u;
    hdr.checksum = pdisk_header_checksum(&hdr);
    if (pdisk_write_header(&hdr) != 0) {
        serial_writeln("[LastOS] pdisk initial header write failed");
        return -1;
    }

    g_ready = 1u;
    return 0;
}

int pdisk_save_blob(const char* blob, u32 len) {
    if (!g_ready || !blob) return -1;
    if (len > PDISK_MAX_BLOB_SIZE) len = PDISK_MAX_BLOB_SIZE;

    pdisk_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.signature = PDISK_SIGNATURE;
    hdr.version = PDISK_VERSION;
    hdr.reserved_sectors = PDISK_RESERVED_SECTORS;
    hdr.blob_length = len;
    hdr.blob_crc = pdisk_crc32((const u8*)blob, len);
    if (pdisk_read_header(&hdr) != 0) {
        hdr.signature = PDISK_SIGNATURE;
        hdr.version = PDISK_VERSION;
        hdr.reserved_sectors = PDISK_RESERVED_SECTORS;
        hdr.vfs_length = 0u;
        hdr.vfs_crc = 0u;
    } else {
        hdr.blob_length = len;
        hdr.blob_crc = pdisk_crc32((const u8*)blob, len);
    }
    hdr.checksum = pdisk_header_checksum(&hdr);

    u32 sectors_to_write = (len + DISK_SECTOR_SIZE - 1u) / DISK_SECTOR_SIZE;
    for (u32 i = 0u; i < sectors_to_write; i++) {
        u8 sector[DISK_SECTOR_SIZE];
        u32 offset = i * DISK_SECTOR_SIZE;
        u32 chunk = len > offset ? len - offset : 0u;
        if (chunk > DISK_SECTOR_SIZE) chunk = DISK_SECTOR_SIZE;
        memset(sector, 0, sizeof(sector));
        if (chunk) memcpy(sector, blob + offset, chunk);
        if (pdisk_write_sector(PDISK_PERSIST_START + i, sector) != 0) return -1;
    }

    return pdisk_write_header(&hdr);
}

int pdisk_load_blob(char* out, u32 out_size) {
    if (!g_ready || !out || out_size == 0u) return -1;

    pdisk_header_t hdr;
    if (pdisk_read_header(&hdr) != 0) return -1;
    if (hdr.blob_length == 0u) {
        out[0] = '\0';
        return 0;
    }

    if (out_size <= hdr.blob_length) return -1;

    u32 sectors_to_read = (hdr.blob_length + DISK_SECTOR_SIZE - 1u) / DISK_SECTOR_SIZE;
    if ((PDISK_PERSIST_START + sectors_to_read) > (PDISK_PERSIST_START + PDISK_BLOB_SECTORS)) return -1;

    for (u32 i = 0u; i < sectors_to_read; i++) {
        u8 sector[DISK_SECTOR_SIZE];
        if (pdisk_read_sector(PDISK_PERSIST_START + i, sector) != 0) return -1;
        u32 offset = i * DISK_SECTOR_SIZE;
        u32 chunk = hdr.blob_length > offset ? hdr.blob_length - offset : 0u;
        if (chunk > DISK_SECTOR_SIZE) chunk = DISK_SECTOR_SIZE;
        memcpy(out + offset, sector, chunk);
    }

    out[hdr.blob_length] = '\0';
    if (pdisk_crc32((const u8*)out, hdr.blob_length) != hdr.blob_crc) return -1;
    return 0;
}

int pdisk_save_vfs_blob(const void* blob, u32 len) {
    if (!g_ready || !blob) { serial_writeln("[LastOS] pdisk vfs: backend unavailable"); return -1; }
    if (len > PDISK_MAX_VFS_BLOB_SIZE) { serial_writeln("[LastOS] pdisk vfs: image too large"); return -1; }

    pdisk_header_t hdr;
    if (pdisk_read_header(&hdr) != 0) { serial_writeln("[LastOS] pdisk vfs: header read failed"); return -1; }

    pdisk_vfs_slot_header_t old_slots[PDISK_VFS_SLOT_COUNT];
    u8 valid[PDISK_VFS_SLOT_COUNT];
    memset(old_slots, 0, sizeof(old_slots));
    memset(valid, 0, sizeof(valid));
    u32 active_slot = 0u;
    u32 generation = 0u;
    for (u32 i = 0u; i < PDISK_VFS_SLOT_COUNT; i++) {
        if (pdisk_read_vfs_slot_header(i, &old_slots[i]) == 0) {
            valid[i] = 1u;
            if (!generation || old_slots[i].generation > generation) {
                generation = old_slots[i].generation;
                active_slot = i;
            }
        }
    }

    u32 target_slot = valid[0] || valid[1] ? (active_slot ^ 1u) : 0u;
    u32 sectors_to_write = (len + DISK_SECTOR_SIZE - 1u) / DISK_SECTOR_SIZE;
    u32 next_generation = generation + 1u;
    if (pdisk_wal_begin(next_generation, target_slot, len, pdisk_crc32((const u8*)blob, len)) != 0) {
        serial_writeln("[LastOS] pdisk vfs: WAL prepare failed");
        return -1;
    }
    for (u32 i = 0u; i < sectors_to_write; i++) {
        u8 sector[DISK_SECTOR_SIZE];
        u32 offset = i * DISK_SECTOR_SIZE;
        u32 chunk = len > offset ? len - offset : 0u;
        if (chunk > DISK_SECTOR_SIZE) chunk = DISK_SECTOR_SIZE;
        memset(sector, 0, sizeof(sector));
        if (chunk) memcpy(sector, ((const u8*)blob) + offset, chunk);
        if (pdisk_write_sector_fast(pdisk_vfs_slot_start(target_slot) + PDISK_VFS_DATA_START + i, sector) != 0) {
            serial_write("[LastOS] pdisk vfs: data write failed at ");
            serial_write_dec(i);
            serial_writeln("");
            return -1;
        }
    }

    pdisk_vfs_slot_header_t slot_hdr;
    memset(&slot_hdr, 0, sizeof(slot_hdr));
    slot_hdr.magic = PDISK_VFS_SLOT_MAGIC;
    slot_hdr.version = PDISK_VFS_SLOT_VERSION;
    slot_hdr.generation = next_generation;
    slot_hdr.length = len;
    slot_hdr.crc = pdisk_crc32((const u8*)blob, len);
    slot_hdr.checksum = pdisk_vfs_slot_checksum(&slot_hdr);
    u8 slot_sector[DISK_SECTOR_SIZE];
    memset(slot_sector, 0, sizeof(slot_sector));
    memcpy(slot_sector, &slot_hdr, sizeof(slot_hdr));
    if (pdisk_write_sector(pdisk_vfs_slot_start(target_slot), slot_sector) != 0) {
        serial_writeln("[LastOS] pdisk vfs: slot header write failed");
        return -1;
    }

    hdr.vfs_length = len;
    hdr.vfs_crc = slot_hdr.crc;
    hdr.vfs_generation = slot_hdr.generation;
    hdr.vfs_slot = target_slot;
    hdr.checksum = pdisk_header_checksum(&hdr);
    if (pdisk_wal_commit(next_generation) != 0) {
        serial_writeln("[LastOS] pdisk vfs: WAL commit failed");
        return -1;
    }
    if (pdisk_write_header(&hdr) != 0) {
        serial_writeln("[LastOS] pdisk vfs: global header write failed");
        return -1;
    }
    return pdisk_wal_clear();
}

int pdisk_load_vfs_blob(void* out, u32 out_size, u32* out_len) {
    if (!g_ready || !out || out_size == 0u) return -1;

    pdisk_vfs_slot_header_t slots[PDISK_VFS_SLOT_COUNT];
    u8 valid[PDISK_VFS_SLOT_COUNT];
    memset(slots, 0, sizeof(slots));
    memset(valid, 0, sizeof(valid));
    for (u32 i = 0u; i < PDISK_VFS_SLOT_COUNT; i++) {
        if (pdisk_read_vfs_slot_header(i, &slots[i]) == 0 && slots[i].length != 0u) valid[i] = 1u;
    }
    if (!valid[0] && !valid[1]) {
        if (out_len) *out_len = 0u;
        return 0;
    }

    u32 first = 0u;
    u32 second = 1u;
    if (valid[1] && (!valid[0] || slots[1].generation > slots[0].generation)) {
        first = 1u;
        second = 0u;
    }
    if (valid[first] && pdisk_load_vfs_slot(first, &slots[first], out, out_size) == 0) {
        if (out_len) *out_len = slots[first].length;
        return 0;
    }
    if (valid[second] && pdisk_load_vfs_slot(second, &slots[second], out, out_size) == 0) {
        if (out_len) *out_len = slots[second].length;
        return 0;
    }
    return -1;
}

int pdisk_rollback_vfs(void) {
    pdisk_vfs_slot_header_t slots[PDISK_VFS_SLOT_COUNT];
    u8 valid[PDISK_VFS_SLOT_COUNT];
    u32 target = 0u;
    pdisk_header_t hdr;
    if (!g_ready || pdisk_read_header(&hdr) != 0) return -1;
    memset(slots, 0, sizeof(slots));
    memset(valid, 0, sizeof(valid));
    for (u32 i = 0u; i < PDISK_VFS_SLOT_COUNT; i++) {
        if (pdisk_read_vfs_slot_header(i, &slots[i]) == 0 && slots[i].length != 0u) valid[i] = 1u;
    }
    if (!valid[0] || !valid[1] || slots[0].generation == slots[1].generation) return -1;
    target = slots[0].generation < slots[1].generation ? 0u : 1u;
    hdr.vfs_length = slots[target].length;
    hdr.vfs_crc = slots[target].crc;
    hdr.vfs_generation = slots[target].generation;
    hdr.vfs_slot = target;
    hdr.checksum = pdisk_header_checksum(&hdr);
    if (pdisk_write_header(&hdr) != 0) return -1;
    return pdisk_wal_clear();
}
