#ifndef MULTICORE_DISPATCH_H
#define MULTICORE_DISPATCH_H

#include "types.h"

void multicore_dispatch_set_cores(u32 cores);
u32 multicore_dispatch_pick(u16 port, u32 index);

#endif
