#ifndef SECURITY_HOOK_H
#define SECURITY_HOOK_H

#include "types.h"

typedef enum {
    HOOK_EVT_ALLOC = 1,
    HOOK_EVT_IO = 2,
    HOOK_EVT_CRASH = 3
} hook_event_t;

typedef void (*hook_fn_t)(u32 event, u32 arg0, u32 arg1);

void security_hook_init(void);
int security_hook_register(const char* module_name, hook_fn_t fn);
int security_hook_unregister(const char* module_name);
void security_hook_notify(u32 event, u32 arg0, u32 arg1);
u32 security_hook_count(void);

#endif
