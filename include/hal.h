#ifndef HAL_H
#define HAL_H

#include "types.h"
#include "cpu_detect.h"

typedef enum {
    HAL_MODE_AUTO = 0,
    HAL_MODE_DESKTOP = 1,
    HAL_MODE_SERVER = 2
} hal_mode_t;

typedef enum {
    HAL_RENDER_AUTO = 0,
    HAL_RENDER_QUALITY = 1,
    HAL_RENDER_PERFORMANCE = 2
} hal_render_policy_t;

typedef struct {
    u32 hetero_core_score;
    u32 predicted_threads;
    u32 thermal_guard_level;
    u32 legacy_tuning_active;
    u32 ghost_ram_enabled;
    u32 ghost_ram_reserved_kb;
    u32 smart_render_level;
    u32 gpu_accel_level;
    u32 data_prefetch_score;
    u32 bandwidth_optimizer_level;
    u32 memory_self_heal_events;
    u32 pnp_sandboxed_drivers;
    u32 driver_avg_health;
    u32 driver_recovery_actions;
    u32 advisor_confidence;
    hal_mode_t mode;
    hal_render_policy_t render_policy;
} hal_status_t;

typedef struct {
    u32 confidence;
    u32 recommended_action;
} hal_driver_advice_t;

void hal_init(u32 ram_bytes, u32 cores, const cpu_info_t* cpu);
void hal_tick(void);
void hal_status(hal_status_t* out);
void hal_set_mode(hal_mode_t mode);
hal_mode_t hal_get_mode(void);
void hal_set_ghost_ram(u8 enabled);
void hal_set_render_policy(hal_render_policy_t policy);
int hal_driver_advice(const char* module_name, hal_driver_advice_t* out);

#endif
