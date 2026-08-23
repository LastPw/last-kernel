#include "pipe.h"
#include "file.h"
#include "string.h"
#include "serial.h"

static pipe_channel_t g_pipe_channels[PIPE_MAX_CHANNELS];
static spinlock_t g_pipe_subsys_lock;
static u8 g_pipe_initialized = 0;

static i32 pipe_read_op(void* priv, char* buf, u32 count, u64 offset) {
    (void)offset;
    if (!priv || !buf || count == 0) return 0;
    pipe_channel_t* p = (pipe_channel_t*)priv;

    spinlock_lock(&p->lock);
    if (p->count == 0) {
        if (p->writers == 0) {
            spinlock_unlock(&p->lock);
            return 0; /* EOF */
        }
        spinlock_unlock(&p->lock);
        return 0;
    }

    u32 read_bytes = count < p->count ? count : p->count;
    for (u32 i = 0; i < read_bytes; i++) {
        buf[i] = p->buffer[(p->read_pos + i) % PIPE_BUFFER_SIZE];
    }
    p->read_pos = (p->read_pos + read_bytes) % PIPE_BUFFER_SIZE;
    p->count -= read_bytes;
    spinlock_unlock(&p->lock);

    return (i32)read_bytes;
}

static int pipe_read_close_op(void* priv) {
    if (!priv) return 0;
    pipe_channel_t* p = (pipe_channel_t*)priv;

    spinlock_lock(&g_pipe_subsys_lock);
    spinlock_lock(&p->lock);
    if (p->readers > 0) p->readers--;
    if (p->readers == 0 && p->writers == 0) {
        p->used = 0;
    }
    spinlock_unlock(&p->lock);
    spinlock_unlock(&g_pipe_subsys_lock);
    return 0;
}

static const file_operations_t s_pipe_read_fops = {
    .open = 0,
    .read = pipe_read_op,
    .write = 0,
    .ioctl = 0,
    .close = pipe_read_close_op,
    .poll = 0
};

static i32 pipe_write_op(void* priv, const char* buf, u32 count, u64 offset) {
    (void)offset;
    if (!priv || !buf || count == 0) return 0;
    pipe_channel_t* p = (pipe_channel_t*)priv;

    spinlock_lock(&p->lock);
    if (p->readers == 0) {
        spinlock_unlock(&p->lock);
        return -1; /* Broken pipe (EPIPE) */
    }

    u32 available = PIPE_BUFFER_SIZE - p->count;
    if (available == 0) {
        spinlock_unlock(&p->lock);
        return 0; /* Buffer full */
    }

    u32 write_bytes = count < available ? count : available;
    for (u32 i = 0; i < write_bytes; i++) {
        p->buffer[p->write_pos] = buf[i];
        p->write_pos = (p->write_pos + 1) % PIPE_BUFFER_SIZE;
    }
    p->count += write_bytes;
    spinlock_unlock(&p->lock);

    return (i32)write_bytes;
}

static int pipe_write_close_op(void* priv) {
    if (!priv) return 0;
    pipe_channel_t* p = (pipe_channel_t*)priv;

    spinlock_lock(&g_pipe_subsys_lock);
    spinlock_lock(&p->lock);
    if (p->writers > 0) p->writers--;
    if (p->readers == 0 && p->writers == 0) {
        p->used = 0;
    }
    spinlock_unlock(&p->lock);
    spinlock_unlock(&g_pipe_subsys_lock);
    return 0;
}

static const file_operations_t s_pipe_write_fops = {
    .open = 0,
    .read = 0,
    .write = pipe_write_op,
    .ioctl = 0,
    .close = pipe_write_close_op,
    .poll = 0
};

void pipe_init(void) {
    if (g_pipe_initialized) return;
    memset(g_pipe_channels, 0, sizeof(g_pipe_channels));
    spinlock_init(&g_pipe_subsys_lock);
    g_pipe_initialized = 1;
    serial_writeln("[Pipe] VFS Pipe Subsystem initialized (32 channels, 4KB buffer)");
}

int kpipe_create(u32* out_read_fd, u32* out_write_fd, u32 owner_task) {
    if (!out_read_fd || !out_write_fd) return -1;
    if (!g_pipe_initialized) pipe_init();

    spinlock_lock(&g_pipe_subsys_lock);
    for (u32 i = 0; i < PIPE_MAX_CHANNELS; i++) {
        if (!g_pipe_channels[i].used) {
            pipe_channel_t* p = &g_pipe_channels[i];
            memset(p, 0, sizeof(*p));
            p->used = 1;
            p->readers = 1;
            p->writers = 1;
            p->owner_task = owner_task;
            spinlock_init(&p->lock);

            u32 rfd = 0, wfd = 0;
            if (kfile_create_custom_fd("pipe:[read]", KFILE_READ, owner_task, &s_pipe_read_fops, p, &rfd) != 0 ||
                kfile_create_custom_fd("pipe:[write]", KFILE_WRITE, owner_task, &s_pipe_write_fops, p, &wfd) != 0) {
                p->used = 0;
                spinlock_unlock(&g_pipe_subsys_lock);
                return -1;
            }

            *out_read_fd = rfd;
            *out_write_fd = wfd;
            spinlock_unlock(&g_pipe_subsys_lock);
            return 0;
        }
    }
    spinlock_unlock(&g_pipe_subsys_lock);
    return -1;
}
