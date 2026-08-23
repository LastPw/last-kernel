#include "module.h"
#include "string.h"
#include "security.h"
#include "scheduler.h"
#include "ktrace.h"
#include "kshield.h"
#include "audit.h"
#include "sha256.h"

static module_info_t mods[MODULE_SLOT_COUNT];

static void set_name(char* dst, const char* src) {
    str_ncpy(dst, src, MODULE_NAME_MAX - 1u);
    dst[MODULE_NAME_MAX - 1u] = '\0';
}

static int find_slot(const char* name) {
    if (!name || !name[0]) {
        return -1;
    }
    for (u32 i = 0; i < MODULE_SLOT_COUNT; i++) {
        if (mods[i].installed && str_cmp(mods[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int find_free_slot(void) {
    for (u32 i = 0; i < MODULE_SLOT_COUNT; i++) {
        if (!mods[i].installed) {
            return (int)i;
        }
    }
    return -1;
}

static int module_is_core(const char* name) {
    if (!name) return 0;
    if (str_cmp(name, "security") == 0) return 1;
    if (str_cmp(name, "resource") == 0) return 1;
    if (str_cmp(name, "hal") == 0) return 1;
    if (str_cmp(name, "ultimate") == 0) return 1;
    if (str_cmp(name, "pcoe2") == 0) return 1;
    if (str_cmp(name, "compat") == 0) return 1;
    return 0;
}

static u8 module_trust_of(const char* name) {
    if (!name || !name[0]) return MODULE_TRUST_UNTRUSTED;
    if (module_is_core(name)) return MODULE_TRUST_CORE;
    if (str_cmp(name, "lastmap") == 0) return MODULE_TRUST_TRUSTED;
    if (str_cmp(name, "lastshark") == 0) return MODULE_TRUST_TRUSTED;
    if (str_cmp(name, "firewall") == 0) return MODULE_TRUST_TRUSTED;
    if (str_cmp(name, "filetools") == 0) return MODULE_TRUST_COMMUNITY;
    return MODULE_TRUST_COMMUNITY;
}

void module_init(void) {
    for (u32 i = 0; i < MODULE_SLOT_COUNT; i++) {
        mods[i].name[0] = '\0';
        mods[i].loaded = 0;
        mods[i].sandboxed = 1;
        mods[i].trust = MODULE_TRUST_UNTRUSTED;
        mods[i].installed = 0;
        mods[i].verified = 0;
        mods[i].rollback_ready = 0;
        mods[i].version = 0u;
        mods[i].load_count = 0;
        mods[i].last_good_checksum = 0u;
        mods[i].rollback_count = 0u;
    }

    module_install("lastmap");
    module_install("lastshark");
    module_install("firewall");
    module_install("filetools");
    module_install("resource");
    module_install("security");
    module_install("hal");
    module_install("ultimate");
    module_install("pcoe2");
    module_install("compat");

    module_load("lastmap");
    module_load("lastshark");
    module_load("firewall");
    module_load("filetools");
    module_load("resource");
    module_load("security");
    module_load("hal");
    module_load("ultimate");
    module_load("pcoe2");
    module_load("compat");
}

int module_install(const char* name) {
    if (!name || !name[0]) {
        return -1;
    }
    if (kshield_authorize_module_install(name) != 0) {
        audit_log(AUDIT_EVT_TASK_VIOLATION, scheduler_current_task(), 0x4D4F4401u);
        return -1;
    }
    if (find_slot(name) >= 0) {
        return 0;
    }

    int s = find_free_slot();
    if (s < 0) {
        return -1;
    }
    set_name(mods[s].name, name);
    mods[s].installed = 1;
    mods[s].loaded = 0;
    mods[s].sandboxed = 1;
    mods[s].trust = module_trust_of(name);
    mods[s].verified = 0;
    mods[s].rollback_ready = 0;
    mods[s].version = 1u;
    mods[s].load_count = 0;
    mods[s].last_good_checksum = 0u;
    mods[s].rollback_count = 0u;
    u8 manifest[SHA256_DIGEST_SIZE];
    security_module_manifest_digest(name, manifest);
    security_register_module_signature_digest(name, manifest);
    return 0;
}

int module_uninstall(const char* name) {
    int s = find_slot(name);
    if (s < 0) {
        return -1;
    }
    mods[s].installed = 0;
    mods[s].loaded = 0;
    mods[s].name[0] = '\0';
    mods[s].verified = 0;
    mods[s].rollback_ready = 0;
    mods[s].version = 0u;
    mods[s].load_count = 0;
    mods[s].last_good_checksum = 0u;
    mods[s].rollback_count = 0u;
    return 0;
}

int module_load(const char* name) {
    int s = find_slot(name);
    if (s < 0) {
        return -1;
    }
    u32 caller = scheduler_current_task();
    if (!security_has_cap(caller, SEC_CAP_MODULE_LOAD) && caller != 0u) {
        ktrace_push(KTRACE_EVT_MODULE_FAIL, 0u, caller, 1u, 0u);
        audit_log(AUDIT_EVT_TASK_VIOLATION, caller, 0x4D4F4402u);
        return -1;
    }
    if (kshield_authorize_module_load(name) != 0) {
        ktrace_push(KTRACE_EVT_MODULE_FAIL, 0u, caller, 3u, 0u);
        audit_log(AUDIT_EVT_TASK_VIOLATION, caller, 0x4D4F4403u);
        return -1;
    }
    if (kshield_lockdown() >= 2u && mods[s].trust < MODULE_TRUST_TRUSTED) {
        ktrace_push(KTRACE_EVT_MODULE_FAIL, 0u, caller, 4u, mods[s].trust);
        audit_log(AUDIT_EVT_TASK_VIOLATION, caller, 0x4D4F4405u);
        return -1;
    }
    u8 manifest[SHA256_DIGEST_SIZE];
    u32 sum;
    security_module_manifest_digest(name, manifest);
    sum = security_module_checksum(name);
    if (!security_verify_module_signature_digest(name, manifest)) {
        mods[s].loaded = 0;
        mods[s].verified = 0;
        if (mods[s].rollback_ready) {
            (void)module_rollback(name);
        }
        ktrace_push(KTRACE_EVT_MODULE_FAIL, 0u, caller, 2u, sum);
        return -1;
    }
    mods[s].loaded = 1;
    mods[s].verified = 1;
    mods[s].rollback_ready = 1;
    mods[s].last_good_checksum = sum;
    if (mods[s].version < 0xFFFFu) {
        mods[s].version++;
    }
    mods[s].load_count++;
    ktrace_push(KTRACE_EVT_MODULE_LOAD, 0u, caller, mods[s].load_count, sum);
    return 0;
}

int module_unload(const char* name) {
    int s = find_slot(name);
    if (s < 0) {
        return -1;
    }
    if (kshield_lockdown() > 0u && module_is_core(name)) {
        audit_log(AUDIT_EVT_TASK_VIOLATION, scheduler_current_task(), 0x4D4F4404u);
        return -1;
    }
    mods[s].loaded = 0;
    return 0;
}

int module_get(const char* name, module_info_t* out) {
    int s = find_slot(name);
    if (s < 0 || !out) {
        return -1;
    }
    *out = mods[s];
    return 0;
}

u32 module_count(void) {
    u32 n = 0;
    for (u32 i = 0; i < MODULE_SLOT_COUNT; i++) {
        if (mods[i].installed) {
            n++;
        }
    }
    return n;
}

int module_get_by_index(u32 idx, module_info_t* out) {
    if (!out) {
        return -1;
    }
    u32 seen = 0;
    for (u32 i = 0; i < MODULE_SLOT_COUNT; i++) {
        if (!mods[i].installed) {
            continue;
        }
        if (seen == idx) {
            *out = mods[i];
            return 0;
        }
        seen++;
    }
    return -1;
}

int module_mark_good(const char* name) {
    int s = find_slot(name);
    if (s < 0) {
        return -1;
    }
    mods[s].verified = 1u;
    mods[s].rollback_ready = 1u;
    mods[s].last_good_checksum = security_module_checksum(name);
    return 0;
}

int module_rollback(const char* name) {
    int s = find_slot(name);
    if (s < 0) {
        return -1;
    }
    if (!mods[s].rollback_ready || mods[s].last_good_checksum == 0u) {
        return -1;
    }
    security_register_module_signature(name, mods[s].last_good_checksum);
    mods[s].loaded = 0u;
    mods[s].verified = 1u;
    if (mods[s].rollback_count < 0xFFFFFFFFu) {
        mods[s].rollback_count++;
    }
    ktrace_push(KTRACE_EVT_MODULE_LOAD, 0u, 0u, 0xAB00u, mods[s].rollback_count);
    return 0;
}
