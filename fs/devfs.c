#include "devfs.h"
#include "string.h"
#include "serial.h"
#include "screen.h"
#include "keyboard.h"
#include "entropy.h"
#include "disk.h"

static devfs_node_t s_devs[DEVFS_MAX_DEVS];
static u32 s_dev_count = 0;
static u8  s_devfs_initialized = 0;

/* ═══════════════════════════════════════════════════════════════════════
 *  Built-in Standard Device Operations
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── /dev/null ── */
static i32 null_read(void* priv, char* buf, u32 count, u64 offset) {
    (void)priv; (void)buf; (void)count; (void)offset;
    return 0; /* EOF */
}

static i32 null_write(void* priv, const char* buf, u32 count, u64 offset) {
    (void)priv; (void)buf; (void)offset;
    return (i32)count; /* Discard all bytes */
}

static const file_operations_t s_null_fops = {
    .open = 0,
    .read = null_read,
    .write = null_write,
    .ioctl = 0,
    .close = 0,
    .poll = 0
};

/* ── /dev/zero ── */
static i32 zero_read(void* priv, char* buf, u32 count, u64 offset) {
    (void)priv; (void)offset;
    if (!buf || count == 0) return 0;
    memset(buf, 0, count);
    return (i32)count;
}

static i32 zero_write(void* priv, const char* buf, u32 count, u64 offset) {
    (void)priv; (void)buf; (void)offset;
    return (i32)count;
}

static const file_operations_t s_zero_fops = {
    .open = 0,
    .read = zero_read,
    .write = zero_write,
    .ioctl = 0,
    .close = 0,
    .poll = 0
};

/* ── /dev/random & /dev/urandom ── */
static i32 random_read(void* priv, char* buf, u32 count, u64 offset) {
    (void)priv; (void)offset;
    if (!buf || count == 0) return 0;
    entropy_fill((u8*)buf, count);
    return (i32)count;
}

static i32 random_write(void* priv, const char* buf, u32 count, u64 offset) {
    (void)priv; (void)buf; (void)offset;
    return (i32)count;
}

static const file_operations_t s_random_fops = {
    .open = 0,
    .read = random_read,
    .write = random_write,
    .ioctl = 0,
    .close = 0,
    .poll = 0
};

/* ── /dev/tty0 & /dev/console ── */
static i32 tty_read(void* priv, char* buf, u32 count, u64 offset) {
    (void)priv; (void)offset;
    if (!buf || count == 0) return 0;

    u32 read_count = 0;
    while (read_count < count) {
        int ch = keyboard_try_read_char();
        if (ch <= 0) break;
        buf[read_count++] = (char)ch;
    }
    return (i32)read_count;
}

static i32 tty_write(void* priv, const char* buf, u32 count, u64 offset) {
    (void)priv; (void)offset;
    if (!buf || count == 0) return 0;

    for (u32 i = 0; i < count; i++) {
        screen_putc(buf[i]);
    }
    return (i32)count;
}

static const file_operations_t s_tty_fops = {
    .open = 0,
    .read = tty_read,
    .write = tty_write,
    .ioctl = 0,
    .close = 0,
    .poll = 0
};

/* ── /dev/ttyS0 (Serial Port COM1) ── */
static i32 serial_dev_read(void* priv, char* buf, u32 count, u64 offset) {
    (void)priv; (void)buf; (void)count; (void)offset;
    return 0;
}

static i32 serial_dev_write(void* priv, const char* buf, u32 count, u64 offset) {
    (void)priv; (void)offset;
    if (!buf || count == 0) return 0;
    for (u32 i = 0; i < count; i++) {
        serial_putc(buf[i]);
    }
    return (i32)count;
}

static const file_operations_t s_serial_fops = {
    .open = 0,
    .read = serial_dev_read,
    .write = serial_dev_write,
    .ioctl = 0,
    .close = 0,
    .poll = 0
};

/* ── /dev/sda (Raw Storage Block Device) ── */
static i32 sda_read(void* priv, char* buf, u32 count, u64 offset) {
    (void)priv;
    if (!buf || count == 0) return 0;

    u32 start_sector = (u32)(offset / DISK_SECTOR_SIZE);
    u32 sector_offset = (u32)(offset % DISK_SECTOR_SIZE);
    u32 bytes_read = 0;
    u8 sector_buf[DISK_SECTOR_SIZE];

    while (bytes_read < count) {
        if (disk_read_sector(start_sector, sector_buf) != 0) {
            break;
        }
        u32 chunk = DISK_SECTOR_SIZE - sector_offset;
        if (chunk > count - bytes_read) {
            chunk = count - bytes_read;
        }
        memcpy(buf + bytes_read, sector_buf + sector_offset, chunk);
        bytes_read += chunk;
        start_sector++;
        sector_offset = 0;
    }
    return (i32)bytes_read;
}

static i32 sda_write(void* priv, const char* buf, u32 count, u64 offset) {
    (void)priv;
    if (!buf || count == 0) return 0;

    u32 start_sector = (u32)(offset / DISK_SECTOR_SIZE);
    u32 sector_offset = (u32)(offset % DISK_SECTOR_SIZE);
    u32 bytes_written = 0;
    u8 sector_buf[DISK_SECTOR_SIZE];

    while (bytes_written < count) {
        u32 chunk = DISK_SECTOR_SIZE - sector_offset;
        if (chunk > count - bytes_written) {
            chunk = count - bytes_written;
        }

        if (sector_offset != 0 || chunk != DISK_SECTOR_SIZE) {
            /* Partial sector: read-modify-write */
            if (disk_read_sector(start_sector, sector_buf) != 0) break;
            memcpy(sector_buf + sector_offset, buf + bytes_written, chunk);
            if (disk_write_sector(start_sector, sector_buf) != 0) break;
        } else {
            /* Full sector */
            if (disk_write_sector(start_sector, (const u8*)(buf + bytes_written)) != 0) break;
        }

        bytes_written += chunk;
        start_sector++;
        sector_offset = 0;
    }
    return (i32)bytes_written;
}

static const file_operations_t s_sda_fops = {
    .open = 0,
    .read = sda_read,
    .write = sda_write,
    .ioctl = 0,
    .close = 0,
    .poll = 0
};

/* ═══════════════════════════════════════════════════════════════════════
 *  DevFS Core Subsystem
 * ═══════════════════════════════════════════════════════════════════════ */

void devfs_init(void) {
    if (s_devfs_initialized) return;

    memset(s_devs, 0, sizeof(s_devs));
    s_dev_count = 0;
    s_devfs_initialized = 1;

    /* Register Core Unix Devices */
    devfs_register_device("null",    DEV_TYPE_CHAR,  &s_null_fops,   0);
    devfs_register_device("zero",    DEV_TYPE_CHAR,  &s_zero_fops,   0);
    devfs_register_device("random",  DEV_TYPE_CHAR,  &s_random_fops, 0);
    devfs_register_device("urandom", DEV_TYPE_CHAR,  &s_random_fops, 0);
    devfs_register_device("tty0",    DEV_TYPE_CHAR,  &s_tty_fops,    0);
    devfs_register_device("console", DEV_TYPE_CHAR,  &s_tty_fops,    0);
    devfs_register_device("ttyS0",   DEV_TYPE_CHAR,  &s_serial_fops, 0);
    devfs_register_device("sda",     DEV_TYPE_BLOCK, &s_sda_fops,    0);

    serial_writeln("[DevFS] Monolithic /dev filesystem initialized with 8 standard nodes");
}

int devfs_register_device(const char* name, u8 dev_type, const file_operations_t* fops, void* priv) {
    if (!name || !name[0] || !fops) return -1;

    for (u32 i = 0; i < DEVFS_MAX_DEVS; i++) {
        if (!s_devs[i].used) {
            devfs_node_t* node = &s_devs[i];
            node->used = 1;
            node->type = dev_type;
            str_lcpy(node->name, name, DEVFS_MAX_NAME);
            node->fops = *fops;
            node->priv_data = priv;

            if (i >= s_dev_count) {
                s_dev_count = i + 1;
            }
            return 0;
        }
    }
    return -1;
}

int devfs_unregister_device(const char* name) {
    if (!name) return -1;
    for (u32 i = 0; i < DEVFS_MAX_DEVS; i++) {
        if (s_devs[i].used && str_cmp(s_devs[i].name, name) == 0) {
            s_devs[i].used = 0;
            return 0;
        }
    }
    return -1;
}

int devfs_is_dev_path(const char* path) {
    if (!path) return 0;
    if (path[0] == '/' && path[1] == 'd' && path[2] == 'e' && path[3] == 'v' && path[4] == '/') {
        return 1;
    }
    if (path[0] == 'd' && path[1] == 'e' && path[2] == 'v' && path[3] == '/') {
        return 1;
    }
    return 0;
}

int devfs_open(const char* path, u32 flags, void** out_priv, const file_operations_t** out_fops) {
    if (!path || !out_fops) return -1;

    const char* devname = path;
    if (devfs_is_dev_path(path)) {
        if (path[0] == '/') {
            devname = path + 5; /* skip "/dev/" */
        } else {
            devname = path + 4; /* skip "dev/" */
        }
    }

    for (u32 i = 0; i < DEVFS_MAX_DEVS; i++) {
        if (s_devs[i].used && str_cmp(s_devs[i].name, devname) == 0) {
            devfs_node_t* node = &s_devs[i];
            if (node->fops.open) {
                void* priv = node->priv_data;
                int r = node->fops.open(devname, flags, &priv);
                if (r != 0) return r;
                if (out_priv) *out_priv = priv;
            } else {
                if (out_priv) *out_priv = node->priv_data;
            }
            *out_fops = &node->fops;
            return 0;
        }
    }
    return -1;
}

int devfs_list(char* out_buf, u32 max_len) {
    if (!out_buf || max_len == 0) return 0;
    out_buf[0] = '\0';
    u32 len = 0;

    for (u32 i = 0; i < DEVFS_MAX_DEVS; i++) {
        if (s_devs[i].used) {
            u32 name_len = str_len(s_devs[i].name);
            if (len + name_len + 2 < max_len) {
                str_lcat(out_buf, s_devs[i].name, max_len);
                str_lcat(out_buf, "\n", max_len);
                len += name_len + 1;
            }
        }
    }
    return (int)len;
}
