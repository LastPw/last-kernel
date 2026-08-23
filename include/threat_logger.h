#ifndef THREAT_LOGGER_H
#define THREAT_LOGGER_H

#include "types.h"

#define THREAT_LOG_CAP 64

typedef enum {
    THREAT_NONE = 0,
    THREAT_SIG_MATCH = 1,
    THREAT_KERNEL_WRITE = 2,
    THREAT_IRQ_ABUSE = 3,
    THREAT_IO_MALFORMED = 4,
    THREAT_CPU_ABUSE = 5,
    THREAT_STACK_CORRUPT = 6,
    THREAT_HEAP_CORRUPT = 7
} threat_type_t;

typedef struct {
    u32 id;
    u32 tick;
    u32 task_id;
    u32 type;
    u32 arg0;
    u32 arg1;
} threat_record_t;

void threat_logger_init(void);
void threat_logger_log(threat_type_t type, u32 task_id, u32 arg0, u32 arg1);
u32 threat_logger_count(void);
int threat_logger_get(u32 reverse_index, threat_record_t* out);

#endif
