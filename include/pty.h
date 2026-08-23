#ifndef PTY_H
#define PTY_H

#include "types.h"
#include "devfs.h"
#include "spinlock.h"

#define PTY_MAX_PAIRS    8u
#define PTY_BUFFER_SIZE  2048u

typedef struct pty_pair {
    u8          used;
    u8          master_open;
    u8          slave_open;
    u8          echo;
    u8          raw_mode;
    u32         id;
    char        slave_name[16];
    
    /* Master-to-Slave buffer (keyboard simulation) */
    char        m2s_buf[PTY_BUFFER_SIZE];
    u32         m2s_read;
    u32         m2s_write;
    u32         m2s_count;

    /* Slave-to-Master buffer (screen output simulation) */
    char        s2m_buf[PTY_BUFFER_SIZE];
    u32         s2m_read;
    u32         s2m_write;
    u32         s2m_count;

    spinlock_t  lock;
} pty_pair_t;

void pty_init(void);

#endif /* PTY_H */
