#include "pty.h"
#include "devfs.h"
#include "string.h"
#include "serial.h"

static pty_pair_t g_ptys[PTY_MAX_PAIRS];
static spinlock_t g_pty_subsys_lock;
static u8 g_pty_initialized = 0;

static const file_operations_t s_pty_slave_fops;

/* ═══════════════════════════════════════════════════════════════════════
 *  PTY Master Operations
 * ═══════════════════════════════════════════════════════════════════════ */

static int ptmx_open(const char* name, u32 flags, void** priv_data) {
    (void)name; (void)flags;
    spinlock_lock(&g_pty_subsys_lock);
    for (u32 i = 0; i < PTY_MAX_PAIRS; i++) {
        if (!g_ptys[i].used) {
            pty_pair_t* p = &g_ptys[i];
            memset(p, 0, sizeof(*p));
            p->used = 1;
            p->master_open = 1;
            p->echo = 1;
            p->raw_mode = 0;
            p->id = i;
            spinlock_init(&p->lock);

            /* Format slave device name (e.g., pts/0) */
            str_lcpy(p->slave_name, "pts/", sizeof(p->slave_name));
            char num[2] = { (char)('0' + i), '\0' };
            str_lcat(p->slave_name, num, sizeof(p->slave_name));

            /* Register slave device in DevFS */
            devfs_register_device(p->slave_name, DEV_TYPE_CHAR, &s_pty_slave_fops, p);

            *priv_data = p;
            spinlock_unlock(&g_pty_subsys_lock);
            return 0;
        }
    }
    spinlock_unlock(&g_pty_subsys_lock);
    return -1;
}

static i32 pty_master_read(void* priv, char* buf, u32 count, u64 offset) {
    (void)offset;
    if (!priv || !buf || count == 0) return 0;
    pty_pair_t* p = (pty_pair_t*)priv;

    spinlock_lock(&p->lock);
    if (p->s2m_count == 0) {
        spinlock_unlock(&p->lock);
        return 0;
    }

    u32 read_bytes = count < p->s2m_count ? count : p->s2m_count;
    for (u32 i = 0; i < read_bytes; i++) {
        buf[i] = p->s2m_buf[(p->s2m_read + i) % PTY_BUFFER_SIZE];
    }
    p->s2m_read = (p->s2m_read + read_bytes) % PTY_BUFFER_SIZE;
    p->s2m_count -= read_bytes;
    spinlock_unlock(&p->lock);

    return (i32)read_bytes;
}

static i32 pty_master_write(void* priv, const char* buf, u32 count, u64 offset) {
    (void)offset;
    if (!priv || !buf || count == 0) return 0;
    pty_pair_t* p = (pty_pair_t*)priv;

    spinlock_lock(&p->lock);
    u32 available = PTY_BUFFER_SIZE - p->m2s_count;
    if (available == 0) {
        spinlock_unlock(&p->lock);
        return 0;
    }

    u32 write_bytes = count < available ? count : available;
    for (u32 i = 0; i < write_bytes; i++) {
        p->m2s_buf[p->m2s_write] = buf[i];
        p->m2s_write = (p->m2s_write + 1) % PTY_BUFFER_SIZE;
    }
    p->m2s_count += write_bytes;
    spinlock_unlock(&p->lock);

    return (i32)write_bytes;
}

static int pty_master_close(void* priv) {
    if (!priv) return 0;
    pty_pair_t* p = (pty_pair_t*)priv;

    spinlock_lock(&g_pty_subsys_lock);
    spinlock_lock(&p->lock);
    p->master_open = 0;
    if (!p->slave_open) {
        devfs_unregister_device(p->slave_name);
        p->used = 0;
    }
    spinlock_unlock(&p->lock);
    spinlock_unlock(&g_pty_subsys_lock);
    return 0;
}

static const file_operations_t s_ptmx_fops = {
    .open = ptmx_open,
    .read = pty_master_read,
    .write = pty_master_write,
    .ioctl = 0,
    .close = pty_master_close,
    .poll = 0
};

/* ═══════════════════════════════════════════════════════════════════════
 *  PTY Slave Operations
 * ═══════════════════════════════════════════════════════════════════════ */

static int pty_slave_open(const char* name, u32 flags, void** priv_data) {
    (void)name; (void)flags;
    if (!priv_data || !*priv_data) return -1;
    pty_pair_t* p = (pty_pair_t*)(*priv_data);
    p->slave_open = 1;
    return 0;
}

static i32 pty_slave_read(void* priv, char* buf, u32 count, u64 offset) {
    (void)offset;
    if (!priv || !buf || count == 0) return 0;
    pty_pair_t* p = (pty_pair_t*)priv;

    spinlock_lock(&p->lock);
    if (p->m2s_count == 0) {
        spinlock_unlock(&p->lock);
        return 0;
    }

    u32 read_bytes = count < p->m2s_count ? count : p->m2s_count;
    for (u32 i = 0; i < read_bytes; i++) {
        buf[i] = p->m2s_buf[(p->m2s_read + i) % PTY_BUFFER_SIZE];
    }
    p->m2s_read = (p->m2s_read + read_bytes) % PTY_BUFFER_SIZE;
    p->m2s_count -= read_bytes;
    spinlock_unlock(&p->lock);

    return (i32)read_bytes;
}

static i32 pty_slave_write(void* priv, const char* buf, u32 count, u64 offset) {
    (void)offset;
    if (!priv || !buf || count == 0) return 0;
    pty_pair_t* p = (pty_pair_t*)priv;

    spinlock_lock(&p->lock);
    u32 available = PTY_BUFFER_SIZE - p->s2m_count;
    if (available == 0) {
        spinlock_unlock(&p->lock);
        return 0;
    }

    u32 write_bytes = count < available ? count : available;
    for (u32 i = 0; i < write_bytes; i++) {
        p->s2m_buf[p->s2m_write] = buf[i];
        p->s2m_write = (p->s2m_write + 1) % PTY_BUFFER_SIZE;
    }
    p->s2m_count += write_bytes;
    spinlock_unlock(&p->lock);

    return (i32)write_bytes;
}

static int pty_slave_close(void* priv) {
    if (!priv) return 0;
    pty_pair_t* p = (pty_pair_t*)priv;

    spinlock_lock(&g_pty_subsys_lock);
    spinlock_lock(&p->lock);
    p->slave_open = 0;
    if (!p->master_open) {
        devfs_unregister_device(p->slave_name);
        p->used = 0;
    }
    spinlock_unlock(&p->lock);
    spinlock_unlock(&g_pty_subsys_lock);
    return 0;
}

static const file_operations_t s_pty_slave_fops = {
    .open = pty_slave_open,
    .read = pty_slave_read,
    .write = pty_slave_write,
    .ioctl = 0,
    .close = pty_slave_close,
    .poll = 0
};

/* ═══════════════════════════════════════════════════════════════════════
 *  PTY Subsystem Init
 * ═══════════════════════════════════════════════════════════════════════ */

void pty_init(void) {
    if (g_pty_initialized) return;
    memset(g_ptys, 0, sizeof(g_ptys));
    spinlock_init(&g_pty_subsys_lock);
    g_pty_initialized = 1;

    /* Register /dev/ptmx */
    devfs_register_device("ptmx", DEV_TYPE_CHAR, &s_ptmx_fops, 0);
    serial_writeln("[PTY] Pseudo-Terminal Master Multiplexer /dev/ptmx registered");
}
