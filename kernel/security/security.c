#include "security.h"
#include "string.h"
#include "sha256.h"
#include "rust_bridge.h"

static security_profile_t prof;

#define SEC_TASK_SLOTS 64u
#define SEC_MOD_SIG_SLOTS 64u
#define SEC_MOD_NAME_MAX 24u

typedef struct {
    u8 used;
    u32 task_id;
    u32 caps;
} task_caps_t;

typedef struct {
    u8 used;
    char name[SEC_MOD_NAME_MAX];
    u32 checksum;
    u8 digest[SHA256_DIGEST_SIZE];
} mod_sig_t;

static task_caps_t g_caps[SEC_TASK_SLOTS];
static mod_sig_t g_sigs[SEC_MOD_SIG_SLOTS];

static int cap_slot(u32 task_id) {
    for (u32 i = 0; i < SEC_TASK_SLOTS; i++) {
        if (g_caps[i].used && g_caps[i].task_id == task_id) return (int)i;
    }
    for (u32 i = 0; i < SEC_TASK_SLOTS; i++) {
        if (!g_caps[i].used) return (int)i;
    }
    /* Table full: reject rather than aliasing onto task_id % SEC_TASK_SLOTS,
     * which would silently hand this task another task's capability entry.
     * Failing here is fail-closed: security_get_task_caps() returns
     * SEC_CAP_NONE for a task with no slot, and a revocation
     * (security_set_task_caps(id, 0)) only reaches this path when the task
     * had no slot to begin with, i.e. already held no capabilities. */
    return -1;
}

static int sig_slot(const char* name) {
    /* First pass: find existing signature by name */
    for (u32 i = 0; i < SEC_MOD_SIG_SLOTS; i++) {
        if (g_sigs[i].used && str_cmp(g_sigs[i].name, name) == 0) return (int)i;
    }
    /* Second pass: find a free slot */
    for (u32 i = 0; i < SEC_MOD_SIG_SLOTS; i++) {
        if (!g_sigs[i].used) return (int)i;
    }
    /* No free slot: reject instead of allowing hash collision to overwrite
     * an existing signature. A collision-based slot would let an attacker
     * register a malicious module whose hash collides with a trusted one,
     * then overwrite the trusted signature. */
    return -1;
}

static void set_mod_name(char* dst, const char* src) {
    str_ncpy(dst, src, SEC_MOD_NAME_MAX - 1u);
    dst[SEC_MOD_NAME_MAX - 1u] = '\0';
}

void security_init(const cpu_info_t* cpu) {
    prof.isolation_enabled = 1;
    prof.guard_pages_enabled = 1;
    prof.irq_sanitizer_enabled = 1;
    prof.memory_scrub_enabled = 1;

    prof.nx_active = 0;
    prof.smep_active = 0;
    prof.smap_active = 0;

    if (!cpu) {
        return;
    }

    if (cpu->has_nx && cpu->has_long_mode) {
        prof.nx_active = 1;
    }
    if (cpu->has_smep) {
        prof.smep_active = 1;
        /* Enable SMEP in CR4: prevents ring-0 execution of ring-3 page
         * table entries (i.e. no kernel execution from user-space pages).
         * Bit 20 = CR4.SMEP. */
        __asm__ __volatile__(
            "mov %%cr4, %%rax\n"
            "or $0x00100000, %%rax\n"   /* Set bit 20 (SMEP) */
            "mov %%rax, %%cr4\n"
            : : : "rax", "memory"
        );
    }
    if (cpu->has_smap) {
        prof.smap_active = 1;
        /* Enable SMAP in CR4: prevents ring-0 data access to ring-3 pages
         * unless explicitly unlocked via STAC/CLAC. Bit 21 = CR4.SMAP. */
        __asm__ __volatile__(
            "mov %%cr4, %%rax\n"
            "or $0x00200000, %%rax\n"   /* Set bit 21 (SMAP) */
            "mov %%rax, %%cr4\n"
            : : : "rax", "memory"
        );
    }

    if (!cpu->has_cpuid) {
        prof.irq_sanitizer_enabled = 0;
    }

    for (u32 i = 0; i < SEC_TASK_SLOTS; i++) {
        g_caps[i].used = 0u;
        g_caps[i].task_id = 0u;
        g_caps[i].caps = SEC_CAP_NONE;
    }
    for (u32 i = 0; i < SEC_MOD_SIG_SLOTS; i++) {
        g_sigs[i].used = 0u;
        g_sigs[i].checksum = 0u;
        g_sigs[i].name[0] = '\0';
    }

    security_set_task_caps(0u, SEC_CAP_ALL);
}

const security_profile_t* security_profile(void) {
    return &prof;
}

int security_set_task_caps(u32 task_id, u32 caps) {
    int s = cap_slot(task_id);
    if (s < 0) return -1;
    g_caps[s].used = 1u;
    g_caps[s].task_id = task_id;
    g_caps[s].caps = caps;

    /* Register with Rust memory-safe security guard */
    if (rust_bridge_available()) {
        (void)rust_bridge_sec_register(task_id, caps);
    }
    return 0;
}

u32 security_get_task_caps(u32 task_id) {
    for (u32 i = 0; i < SEC_TASK_SLOTS; i++) {
        if (g_caps[i].used && g_caps[i].task_id == task_id) return g_caps[i].caps;
    }
    return SEC_CAP_NONE;
}

int security_has_cap(u32 task_id, u32 cap) {
    if (rust_bridge_available()) {
        if (!rust_bridge_sec_check_cap(task_id, cap)) {
            return 0;
        }
    }
    u32 c = security_get_task_caps(task_id);
    return (c & cap) == cap;
}

u32 security_module_checksum(const char* name) {
    u8 digest[SHA256_DIGEST_SIZE];
    security_module_manifest_digest(name, digest);
    return ((u32)digest[0] << 24u) | ((u32)digest[1] << 16u) |
           ((u32)digest[2] << 8u) | (u32)digest[3];
}

void security_module_manifest_digest(const char* name, u8 digest[32]) {
    static const u8 domain[] = "LastOS built-in module manifest v2:";
    sha256_ctx_t ctx;
    if (!digest) return;
    sha256_init(&ctx);
    sha256_update(&ctx, domain, (u32)(sizeof(domain) - 1u));
    if (name) sha256_update(&ctx, (const u8*)name, str_len(name));
    sha256_final(&ctx, digest);
}

void security_register_module_signature_digest(const char* name, const u8 digest[32]) {
    if (!name || !name[0] || !digest) return;
    int s = sig_slot(name);
    /* sig_slot now returns -1 when the table is full instead of allowing
     * hash collision overwrites. Silently fail registration rather than
     * corrupting an existing signature. */
    if (s < 0) return;
    g_sigs[s].used = 1u;
    set_mod_name(g_sigs[s].name, name);
    memcpy(g_sigs[s].digest, digest, SHA256_DIGEST_SIZE);
    g_sigs[s].checksum = ((u32)digest[0] << 24u) | ((u32)digest[1] << 16u) |
                         ((u32)digest[2] << 8u) | (u32)digest[3];
}

int security_verify_module_signature_digest(const char* name, const u8 digest[32]) {
    if (!name || !name[0] || !digest) return 0;
    for (u32 i = 0; i < SEC_MOD_SIG_SLOTS; i++) {
        if (!g_sigs[i].used) continue;
        if (str_cmp(g_sigs[i].name, name) == 0) {
            return crypto_equal(g_sigs[i].digest, digest, SHA256_DIGEST_SIZE);
        }
    }
    return 0;
}

void security_register_module_signature(const char* name, u32 checksum) {
    if (!name || !name[0]) return;
    int s = sig_slot(name);
    /* sig_slot now returns -1 when the table is full (see comment above). */
    if (s < 0) return;
    g_sigs[s].used = 1u;
    set_mod_name(g_sigs[s].name, name);
    g_sigs[s].checksum = checksum;
    security_module_manifest_digest(name, g_sigs[s].digest);
}

int security_verify_module_signature(const char* name, u32 checksum) {
    if (!name || !name[0]) return 0;
    for (u32 i = 0; i < SEC_MOD_SIG_SLOTS; i++) {
        if (!g_sigs[i].used) continue;
        if (str_cmp(g_sigs[i].name, name) == 0) {
            u8 digest[SHA256_DIGEST_SIZE];
            security_module_manifest_digest(name, digest);
            return g_sigs[i].checksum == checksum &&
                   crypto_equal(g_sigs[i].digest, digest, SHA256_DIGEST_SIZE);
        }
    }
    return 0;
}
