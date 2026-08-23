#include "epoll.h"
#include "netstack.h"
#include "timer.h"
#include "string.h"
#include "heap.h"
#include "spinlock.h"

#define MAX_EPOLL_INSTANCES 16u
#define MAX_EPOLL_ITEMS     64u

typedef struct {
    int fd;
    epoll_event_t event;
    u8 active;
} epoll_item_t;

typedef struct {
    u8 used;
    u32 epfd;
    spinlock_t lock;
    u32 item_count;
    epoll_item_t items[MAX_EPOLL_ITEMS];
} epoll_instance_t;

static epoll_instance_t g_epoll_table[MAX_EPOLL_INSTANCES];
static spinlock_t g_epoll_global_lock;
static u32 g_next_epfd = 100u;

void epoll_init(void) {
    spinlock_init(&g_epoll_global_lock);
    memset(g_epoll_table, 0, sizeof(g_epoll_table));
    for (u32 i = 0u; i < MAX_EPOLL_INSTANCES; i++) {
        spinlock_init(&g_epoll_table[i].lock);
    }
}

int epoll_create1(int flags) {
    (void)flags;
    spinlock_lock(&g_epoll_global_lock);
    for (u32 i = 0u; i < MAX_EPOLL_INSTANCES; i++) {
        if (!g_epoll_table[i].used) {
            g_epoll_table[i].used = 1u;
            g_epoll_table[i].epfd = g_next_epfd++;
            g_epoll_table[i].item_count = 0u;
            memset(g_epoll_table[i].items, 0, sizeof(g_epoll_table[i].items));
            u32 assigned_epfd = g_epoll_table[i].epfd;
            spinlock_unlock(&g_epoll_global_lock);
            return (int)assigned_epfd;
        }
    }
    spinlock_unlock(&g_epoll_global_lock);
    return -1;
}

static epoll_instance_t* find_epoll(int epfd) {
    if (epfd <= 0) return 0;
    for (u32 i = 0u; i < MAX_EPOLL_INSTANCES; i++) {
        if (g_epoll_table[i].used && g_epoll_table[i].epfd == (u32)epfd) {
            return &g_epoll_table[i];
        }
    }
    return 0;
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event* event) {
    epoll_instance_t* ep = find_epoll(epfd);
    if (!ep || fd < 0) return -1;

    spinlock_lock(&ep->lock);
    if (op == EPOLL_CTL_ADD) {
        if (!event || ep->item_count >= MAX_EPOLL_ITEMS) {
            spinlock_unlock(&ep->lock);
            return -1;
        }
        for (u32 i = 0u; i < MAX_EPOLL_ITEMS; i++) {
            if (!ep->items[i].active) {
                ep->items[i].fd = fd;
                ep->items[i].event = *event;
                ep->items[i].active = 1u;
                ep->item_count++;
                spinlock_unlock(&ep->lock);
                return 0;
            }
        }
    } else if (op == EPOLL_CTL_DEL) {
        for (u32 i = 0u; i < MAX_EPOLL_ITEMS; i++) {
            if (ep->items[i].active && ep->items[i].fd == fd) {
                ep->items[i].active = 0u;
                if (ep->item_count > 0) ep->item_count--;
                spinlock_unlock(&ep->lock);
                return 0;
            }
        }
    } else if (op == EPOLL_CTL_MOD) {
        if (!event) {
            spinlock_unlock(&ep->lock);
            return -1;
        }
        for (u32 i = 0u; i < MAX_EPOLL_ITEMS; i++) {
            if (ep->items[i].active && ep->items[i].fd == fd) {
                ep->items[i].event = *event;
                spinlock_unlock(&ep->lock);
                return 0;
            }
        }
    }
    spinlock_unlock(&ep->lock);
    return -1;
}

int epoll_wait(int epfd, struct epoll_event* events, int maxevents, int timeout_ms) {
    epoll_instance_t* ep = find_epoll(epfd);
    if (!ep || !events || maxevents <= 0) return -1;

    u32 start_ticks = timer_ticks();
    u32 timeout_ticks = (timeout_ms > 0) ? (u32)(timeout_ms / 10) : 0u;

    for (;;) {
        spinlock_lock(&ep->lock);
        int event_count = 0;
        for (u32 i = 0u; i < MAX_EPOLL_ITEMS && event_count < maxevents; i++) {
            if (ep->items[i].active) {
                // Query netstack socket status for readability/writability
                u32 ready_events = 0u;
                u8 st = 0u;
                if (netstack_socket_state(ep->items[i].fd, &st) == 0 && st != 0) {
                    ready_events |= (EPOLLIN | EPOLLOUT);
                }

                if ((ready_events & ep->items[i].event.events) != 0) {
                    events[event_count] = ep->items[i].event;
                    events[event_count].events = ready_events;
                    event_count++;
                }
            }
        }
        spinlock_unlock(&ep->lock);

        if (event_count > 0 || timeout_ms == 0) {
            return event_count;
        }

        if (timeout_ms > 0 && (timer_ticks() - start_ticks) >= timeout_ticks) {
            return 0;
        }

        // Poll NIC for new packets
        (void)netstack_poll(4u);
    }
}
