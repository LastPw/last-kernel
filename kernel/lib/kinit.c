#include "kinit.h"

#define KINIT_MAX_MODULES 96u

typedef struct {
    kinit_module_t mod;
    i32 rc;
    u8 ran;
} kinit_slot_t;

static kinit_slot_t g_mod[KINIT_MAX_MODULES];
static u32 g_count = 0u;
static kinit_status_t g_st;

void kinit_reset(void) {
    g_count = 0u;
    g_st.total = 0u;
    g_st.done = 0u;
    g_st.failed = 0u;
    g_st.last_rc = 0u;
    g_st.last_index = 0u;
    g_st.last_level = KINIT_LVL_EARLY;
    for (u32 i = 0u; i < KINIT_MAX_MODULES; i++) {
        g_mod[i].mod.name = 0;
        g_mod[i].mod.level = KINIT_LVL_EARLY;
        g_mod[i].mod.init = 0;
        g_mod[i].rc = 0;
        g_mod[i].ran = 0u;
    }
}

int kinit_register(const kinit_module_t* mods, u32 count) {
    if (!mods || count == 0u) return -1;
    if (g_count + count > KINIT_MAX_MODULES) return -1;
    for (u32 i = 0u; i < count; i++) {
        if (!mods[i].name || !mods[i].init) return -1;
        g_mod[g_count].mod = mods[i];
        g_mod[g_count].rc = 0;
        g_mod[g_count].ran = 0u;
        g_count++;
    }
    g_st.total = g_count;
    return 0;
}

static int run_one(u32 idx) {
    if (idx >= g_count) return -1;
    if (g_mod[idx].ran) return g_mod[idx].rc;
    i32 rc = g_mod[idx].mod.init();
    g_mod[idx].rc = rc;
    g_mod[idx].ran = 1u;
    g_st.last_index = idx;
    g_st.last_level = g_mod[idx].mod.level;
    g_st.last_rc = (u32)rc;
    if (rc == 0) g_st.done++;
    else g_st.failed++;
    return rc;
}

int kinit_run_level(kinit_level_t level) {
    int rc = 0;
    for (u32 i = 0u; i < g_count; i++) {
        if (g_mod[i].mod.level != level) continue;
        if (run_one(i) != 0) rc = -1;
    }
    return rc;
}

int kinit_run_all(void) {
    int rc = 0;
    for (u32 lvl = 0u; lvl < (u32)KINIT_LVL_MAX; lvl++) {
        if (kinit_run_level((kinit_level_t)lvl) != 0) rc = -1;
    }
    return rc;
}

void kinit_status(kinit_status_t* out) {
    if (!out) return;
    *out = g_st;
}

int kinit_module_info(u32 index, kinit_module_t* out, i32* out_rc) {
    if (index >= g_count || !out) return -1;
    *out = g_mod[index].mod;
    if (out_rc) *out_rc = g_mod[index].rc;
    return 0;
}
