#ifndef PIPE_H
#define PIPE_H

#include "types.h"
#include "devfs.h"
#include "spinlock.h"

#define PIPE_MAX_CHANNELS 32u
#define PIPE_BUFFER_SIZE  4096u

typedef struct pipe_channel {
    u8          used;
    u8          readers;
    u8          writers;
    u8          reserved;
    u32         owner_task;
    u32         read_pos;
    u32         write_pos;
    u32         count;
    char        buffer[PIPE_BUFFER_SIZE];
    spinlock_t  lock;
} pipe_channel_t;

void pipe_init(void);
int  kpipe_create(u32* out_read_fd, u32* out_write_fd, u32 owner_task);

#endif /* PIPE_H */
