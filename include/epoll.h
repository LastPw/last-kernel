#ifndef EPOLL_H
#define EPOLL_H

#include "types.h"

#define EPOLL_CLOEXEC 02000000

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EPOLLIN     0x001
#define EPOLLPRI    0x002
#define EPOLLOUT    0x004
#define EPOLLERR    0x008
#define EPOLLHUP    0x010
#define EPOLLET     (1u << 31)

typedef union epoll_data {
    void* ptr;
    int fd;
    u32 u32_val;
    u64 u64_val;
} epoll_data_t;

struct epoll_event {
    u32 events;
    epoll_data_t data;
} __attribute__((packed));

typedef struct epoll_event epoll_event_t;

void epoll_init(void);
int epoll_create1(int flags);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event* event);
int epoll_wait(int epfd, struct epoll_event* events, int maxevents, int timeout_ms);

#endif /* EPOLL_H */
