#ifndef AP_WORKER_H
#define AP_WORKER_H

#include "types.h"

/* Application-processor worker cores.
 *
 * Each AP that completes the SMP trampoline enters ap_worker_entry()
 * instead of halting forever.  Workers poll a per-core slot for submitted
 * function pointers, execute them, and report completion -- giving the
 * kernel genuine multi-core execution without racing the single-run-queue
 * scheduler (which remains BSP-only until per-CPU scheduling lands).
 *
 * Work items must be pure: no sleeping, no blocking on other cores. */

typedef void (*ap_work_fn)(void* arg);

void     ap_worker_entry(u32 hw_index);   /* AP entrypoint (never returns) */
int      ap_worker_submit(u32 hw_index, ap_work_fn fn, void* arg); /* sync */
u32      ap_worker_tick_count(u32 hw_index);
u32      ap_worker_online_mask(void);
u32      ap_worker_core_count(void);

#endif /* AP_WORKER_H */
