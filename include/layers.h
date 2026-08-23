#ifndef LAYERS_H
#define LAYERS_H

#include "types.h"

typedef enum {
    LAYER_KERNEL = 0,
    LAYER_DRIVER = 1,
    LAYER_APPLICATION = 2
} layer_kind_t;

typedef struct {
    u32 total_components;
    u32 kernel_components;
    u32 driver_components;
    u32 app_components;
    u32 kernel_weight_percent;
    u32 driver_weight_percent;
    u32 app_weight_percent;
    u8 architecture_ok;
} layer_stats_t;

void layers_init(void);
int layers_register(const char* name, layer_kind_t kind, u32 weight);
void layers_stats(layer_stats_t* out);
int layers_architecture_ok(void);

#endif
