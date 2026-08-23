#include "musl_sys.h"
#include "epoll.h"
#include "timer.h"
#include "heap.h"
#include "task.h"
#include "scheduler.h"
#include "vfs.h"
#include "string.h"

int musl_sys_dispatch(u32 sys_nr, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64* out_result) {
    (void)a4; (void)a5;
    if (!out_result) return -1;

    switch (sys_nr) {
        case SYS_getpid:
            *out_result = (u64)scheduler_current_task();
            return 0;

        case SYS_brk:
            // Process heap expansion
            if (a0 == 0) {
                *out_result = 0x4000000000ULL; // Default user heap base
            } else {
                *out_result = a0;
            }
            return 0;

        case SYS_mmap:
            // Anonymous or file mmap allocation
            if (a1 > 0) {
                void* ptr = heap_alloc((size_t)a1);
                *out_result = ptr ? (u64)(uintptr_t)ptr : (u64)-1;
            } else {
                *out_result = (u64)-1;
            }
            return 0;

        case SYS_munmap:
            if (a0 != 0) {
                heap_free((void*)(uintptr_t)a0);
            }
            *out_result = 0;
            return 0;

        case SYS_clock_gettime: {
            u32 ticks = timer_ticks();
            u64 sec = ticks / 100u;
            u64 nsec = (ticks % 100u) * 10000000ULL;
            if (a1 != 0) {
                u64* ts = (u64*)(uintptr_t)a1;
                ts[0] = sec;
                ts[1] = nsec;
            }
            *out_result = 0;
            return 0;
        }

        case SYS_epoll_create1:
            *out_result = (u64)epoll_create1((int)a0);
            return 0;

        case SYS_epoll_ctl:
            *out_result = (u64)epoll_ctl((int)a0, (int)a1, (int)a2, (struct epoll_event*)(uintptr_t)a3);
            return 0;

        case SYS_epoll_wait:
            *out_result = (u64)epoll_wait((int)a0, (struct epoll_event*)(uintptr_t)a1, (int)a2, (int)a3);
            return 0;

        case SYS_exit:
            scheduler_exit_task(scheduler_current_task(), (i32)a0);
            *out_result = 0;
            return 0;

        default:
            *out_result = (u64)-1;
            return -1;
    }
}
