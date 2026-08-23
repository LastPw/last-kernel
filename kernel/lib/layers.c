#include "layers.h"
#include "string.h"

#define LAYERS_MAX_COMPONENTS 128u
#define LAYERS_NAME_MAX 24u

typedef struct {
    u8 used;
    char name[LAYERS_NAME_MAX];
    layer_kind_t kind;
    u32 weight;
} layer_component_t;

static layer_component_t g_components[LAYERS_MAX_COMPONENTS];
static layer_stats_t g_stats;

static void recalc(void) {
    u32 k_weight = 0u, d_weight = 0u, a_weight = 0u;
    g_stats.total_components = 0u;
    g_stats.kernel_components = 0u;
    g_stats.driver_components = 0u;
    g_stats.app_components = 0u;

    for (u32 i = 0; i < LAYERS_MAX_COMPONENTS; i++) {
        if (!g_components[i].used) continue;
        g_stats.total_components++;
        if (g_components[i].kind == LAYER_KERNEL) {
            g_stats.kernel_components++;
            k_weight += g_components[i].weight;
        } else if (g_components[i].kind == LAYER_DRIVER) {
            g_stats.driver_components++;
            d_weight += g_components[i].weight;
        } else {
            g_stats.app_components++;
            a_weight += g_components[i].weight;
        }
    }

    u32 total_weight = k_weight + d_weight + a_weight;
    if (total_weight == 0u) {
        g_stats.kernel_weight_percent = 0u;
        g_stats.driver_weight_percent = 0u;
        g_stats.app_weight_percent = 0u;
        g_stats.architecture_ok = 0u;
        return;
    }

    g_stats.kernel_weight_percent = (k_weight * 100u) / total_weight;
    g_stats.driver_weight_percent = (d_weight * 100u) / total_weight;
    g_stats.app_weight_percent = (a_weight * 100u) / total_weight;

    /* Architecture target: kernel around 80% or above, drivers/apps layered. */
    g_stats.architecture_ok = (g_stats.kernel_weight_percent >= 80u &&
                               g_stats.driver_components > 0u &&
                               g_stats.app_components > 0u) ? 1u : 0u;
}

void layers_init(void) {
    for (u32 i = 0; i < LAYERS_MAX_COMPONENTS; i++) {
        g_components[i].used = 0u;
        g_components[i].name[0] = '\0';
        g_components[i].kind = LAYER_APPLICATION;
        g_components[i].weight = 0u;
    }
    recalc();
}

int layers_register(const char* name, layer_kind_t kind, u32 weight) {
    if (!name || !name[0]) return -1;
    if (weight == 0u) weight = 1u;

    for (u32 i = 0; i < LAYERS_MAX_COMPONENTS; i++) {
        if (!g_components[i].used) continue;
        if (str_cmp(g_components[i].name, name) == 0) {
            g_components[i].kind = kind;
            g_components[i].weight = weight;
            recalc();
            return 0;
        }
    }

    for (u32 i = 0; i < LAYERS_MAX_COMPONENTS; i++) {
        if (g_components[i].used) continue;
        g_components[i].used = 1u;
        str_ncpy(g_components[i].name, name, LAYERS_NAME_MAX - 1u);
        g_components[i].name[LAYERS_NAME_MAX - 1u] = '\0';
        g_components[i].kind = kind;
        g_components[i].weight = weight;
        recalc();
        return 0;
    }
    return -1;
}

void layers_stats(layer_stats_t* out) {
    if (!out) return;
    *out = g_stats;
}

int layers_architecture_ok(void) {
    return g_stats.architecture_ok ? 1 : 0;
}

