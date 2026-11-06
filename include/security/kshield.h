#ifndef KSHIELD_H
#define KSHIELD_H

#include "types.h"
#include "threat_logger.h"

typedef enum {
    KSHIELD_MODE_BALANCED = 0,
    KSHIELD_MODE_HARDENED = 1
} kshield_mode_t;

typedef struct {
    u8 enabled;
    u8 lockdown_level;
    kshield_mode_t mode;
    u8 reserved;
    u32 integrity_score;
    u32 risk_score;
    u32 blocked_module_loads;
    u32 blocked_module_installs;
    u32 blocked_irqs;
    u32 blocked_io_ops;
    u32 quarantined_tasks;
    u32 auto_recoveries;
    u32 policy_version;
} kshield_status_t;

void kshield_init(void);
void kshield_tick(void);
void kshield_status(kshield_status_t* out);
void kshield_set_mode(kshield_mode_t mode);
void kshield_set_lockdown(u8 level);
u8 kshield_lockdown(void);
int kshield_authorize_module_load(const char* module_name);
int kshield_authorize_module_install(const char* module_name);
int kshield_on_irq(u8 irq, u32 global_irq_count);
int kshield_on_io(u32 task_id, u32 bytes, u32 flags);
void kshield_on_alloc(u32 task_id, u32 size);
void kshield_on_threat(threat_type_t type, u32 task_id, u32 arg0, u32 arg1);

#endif
