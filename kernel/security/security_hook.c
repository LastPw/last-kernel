#include "security_hook.h"
#include "module.h"
#include "string.h"

#define HOOK_MAX 16

typedef struct {
    char module[MODULE_NAME_MAX];
    hook_fn_t fn;
    u8 used;
} hook_entry_t;

static hook_entry_t hooks[HOOK_MAX];

void security_hook_init(void) {
    for (u32 i = 0; i < HOOK_MAX; i++) {
        hooks[i].used = 0;
        hooks[i].fn = 0;
        hooks[i].module[0] = '\0';
    }
}

int security_hook_register(const char* module_name, hook_fn_t fn) {
    if (!module_name || !module_name[0] || !fn) {
        return -1;
    }
    module_info_t m;
    if (module_get(module_name, &m) != 0) {
        return -1;
    }
    for (u32 i = 0; i < HOOK_MAX; i++) {
        if (hooks[i].used && str_cmp(hooks[i].module, module_name) == 0) {
            hooks[i].fn = fn;
            return 0;
        }
    }
    for (u32 i = 0; i < HOOK_MAX; i++) {
        if (!hooks[i].used) {
            hooks[i].used = 1;
            hooks[i].fn = fn;
            str_ncpy(hooks[i].module, module_name, MODULE_NAME_MAX);
            return 0;
        }
    }
    return -1;
}

int security_hook_unregister(const char* module_name) {
    if (!module_name || !module_name[0]) {
        return -1;
    }
    for (u32 i = 0; i < HOOK_MAX; i++) {
        if (hooks[i].used && str_cmp(hooks[i].module, module_name) == 0) {
            hooks[i].used = 0;
            hooks[i].fn = 0;
            hooks[i].module[0] = '\0';
            return 0;
        }
    }
    return -1;
}

void security_hook_notify(u32 event, u32 arg0, u32 arg1) {
    for (u32 i = 0; i < HOOK_MAX; i++) {
        if (!hooks[i].used || !hooks[i].fn) {
            continue;
        }
        hooks[i].fn(event, arg0, arg1);
    }
}

u32 security_hook_count(void) {
    u32 n = 0;
    for (u32 i = 0; i < HOOK_MAX; i++) {
        if (hooks[i].used) {
            n++;
        }
    }
    return n;
}
