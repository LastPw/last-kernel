#ifndef SECURITY_H
#define SECURITY_H

#include "types.h"
#include "cpu_detect.h"

typedef struct {
    u8 isolation_enabled;
    u8 guard_pages_enabled;
    u8 irq_sanitizer_enabled;
    u8 memory_scrub_enabled;
    u8 nx_active;
    u8 smep_active;
    u8 smap_active;
} security_profile_t;

#define SEC_CAP_NONE         0x00000000u
#define SEC_CAP_MODULE_LOAD  0x00000001u
#define SEC_CAP_NET_ADMIN    0x00000002u
#define SEC_CAP_FS_ADMIN     0x00000004u
#define SEC_CAP_SYS_ADMIN    0x00000008u
#define SEC_CAP_ALL          0xFFFFFFFFu

void security_init(const cpu_info_t* cpu);
const security_profile_t* security_profile(void);
int security_set_task_caps(u32 task_id, u32 caps);
u32 security_get_task_caps(u32 task_id);
int security_has_cap(u32 task_id, u32 cap);
u32 security_module_checksum(const char* name);
void security_module_manifest_digest(const char* name, u8 digest[32]);
void security_register_module_signature_digest(const char* name, const u8 digest[32]);
int security_verify_module_signature_digest(const char* name, const u8 digest[32]);
void security_register_module_signature(const char* name, u32 checksum);
int security_verify_module_signature(const char* name, u32 checksum);

#endif
