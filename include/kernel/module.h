#ifndef MODULE_H
#define MODULE_H

#include "types.h"

#define MODULE_NAME_MAX 16
#define MODULE_SLOT_COUNT 16
#define MODULE_TRUST_UNTRUSTED 0u
#define MODULE_TRUST_COMMUNITY 1u
#define MODULE_TRUST_TRUSTED   2u
#define MODULE_TRUST_CORE      3u

typedef struct {
    char name[MODULE_NAME_MAX];
    u8 loaded;
    u8 sandboxed;
    u8 trust;
    u8 installed;
    u8 verified;
    u8 rollback_ready;
    u16 version;
    u32 load_count;
    u32 last_good_checksum;
    u32 rollback_count;
} module_info_t;

void module_init(void);
int module_install(const char* name);
int module_uninstall(const char* name);
int module_load(const char* name);
int module_unload(const char* name);
int module_get(const char* name, module_info_t* out);
u32 module_count(void);
int module_get_by_index(u32 idx, module_info_t* out);
int module_mark_good(const char* name);
int module_rollback(const char* name);

#endif
