#include "multicore_dispatch.h"

static u32 active_cores = 1;

void multicore_dispatch_set_cores(u32 cores) {
    if (cores == 0) {
        cores = 1;
    }
    if (cores > 32) {
        cores = 32;
    }
    active_cores = cores;
}

u32 multicore_dispatch_pick(u16 port, u32 index) {
    if (active_cores <= 1) {
        return 0;
    }
    return (u32)((port + (u16)index) % active_cores);
}
