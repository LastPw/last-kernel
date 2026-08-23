#ifndef SMP_H
#define SMP_H

#include "types.h"

typedef struct {
    u32 detected;
    u32 started;
    u32 online;
    u32 lapic_addr;
} smp_status_t;

void smp_init(void);
int smp_start_aps(void);
u32 smp_online_count(void);
void smp_status(smp_status_t* out);

#endif
