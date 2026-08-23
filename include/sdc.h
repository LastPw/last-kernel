#ifndef SDC_H
#define SDC_H

#include "types.h"
#include "security_hook.h"

typedef enum {
    SDC_MODE_DESKTOP = 0,
    SDC_MODE_SERVER = 1
} sdc_mode_t;

typedef struct {
    u8 cpu_safe;
    u8 ram_safe;
    u32 network_suspicious;
    u32 files_integrity_percent;
    u32 blocked_module_loads;
    u32 quarantined_tasks;
    u32 throttled_tasks;
    u32 alerts_total;
    sdc_mode_t mode;
} sdc_status_t;

void sdc_init(void);
void sdc_tick(void);
void sdc_set_mode(sdc_mode_t mode);
sdc_mode_t sdc_get_mode(void);
void sdc_status(sdc_status_t* out);
int sdc_authorize_module_load(const char* module_name);
int sdc_authorize_module_install(const char* module_name);
void sdc_on_alloc(u32 task_id, u32 size);
void sdc_on_io(u32 task_id, u32 bytes);
void sdc_on_packet(u32 packet_len, u8 suspicious);
void sdc_on_process_crash(u32 reason);
int sdc_register_safe_hook(const char* module_name, hook_fn_t fn);

#endif
