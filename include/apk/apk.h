#ifndef APK_H
#define APK_H

#include "types.h"
#include "idt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Micro-Domain Identifiers ── */
#define APK_DOMAIN_CORE     0u
#define APK_DOMAIN_STORAGE  1u
#define APK_DOMAIN_NET      2u
#define APK_DOMAIN_GFX      3u
#define APK_DOMAIN_COMPAT   4u
#define APK_DOMAIN_USER     5u
#define APK_MAX_DOMAINS     6u

/* ── Kernel Morphing Modes ── */
#define APK_MORPH_BALANCED    0u
#define APK_MORPH_THROUGHPUT  1u /* Heavy batching, cache locality, large quantum */
#define APK_MORPH_LOW_LATENCY 2u /* Sub-millisecond preemptive esports/audio fastlane */
#define APK_MORPH_HARDENED    3u /* Zero-trust, strict PKEY memory isolation */
#define APK_MORPH_GREEN       4u /* Energy saving, core packing, low frequency */
#define APK_MAX_MORPH_MODES   5u

/* ── Micro-Domain Descriptor ── */
typedef struct {
    u32 domain_id;
    char name[24];
    u8 active;
    u8 quarantined;
    u32 pkey_tag;
    u32 fault_count;
    u32 heal_count;
    u32 memory_quota_bytes;
    u32 allocated_bytes;
    u32 last_checkpoint_tick;
    registers_t checkpoint_regs;
} apk_domain_t;

/* ── APK System Status & Telemetry ── */
typedef struct {
    u32 current_morph_mode;
    u32 auto_morph_enabled;
    u32 total_domains;
    u32 active_domains;
    u32 total_heals;
    u32 total_faults_isolated;
    u32 total_morph_switches;
    u32 avg_heal_latency_us;
    u32 pkey_enforced;
    u32 quarantined_domains;
} apk_status_t;

/* ── APK Core APIs ── */
int  apk_init(void);
int  apk_set_morph_mode(u32 mode);
u32  apk_get_morph_mode(void);
const char* apk_morph_mode_name(u32 mode);

int  apk_checkpoint_domain(u32 domain_id, const registers_t* regs);
int  apk_try_heal_domain(u32 domain_id, registers_t* regs);
void apk_get_status(apk_status_t* out);
int  apk_get_domain_info(u32 domain_id, apk_domain_t* out);

/* ── Hardware Domain Tracking ── */
void apk_set_current_domain(u32 domain_id);
u32  apk_get_current_domain(void);

/* ── Hardware Exception Interceptor ── */
int  apk_handle_hardware_exception(u64 vector, registers_t* regs, u64 fault_addr);

/* ── Autonomous Auto-Morphing ── */
void apk_enable_auto_morph(u8 enable);
u8   apk_is_auto_morph_enabled(void);
void apk_telemetry_tick(u32 input_events, u32 io_pressure, u32 cpu_load);

/* ── Driver Quarantine & Hot-Reload ── */
int  apk_quarantine_domain(u32 domain_id);
int  apk_hot_reload_domain(u32 domain_id);

/* ── Transactional Shadow Undo-Log ── */
#define APK_MAX_TX_LOG_ENTRIES 256

typedef struct {
    u64 addr;
    u64 orig_val;
    u8  size;
} apk_tx_entry_t;

typedef struct {
    u32 tx_id;
    u32 domain_id;
    u32 entry_count;
    apk_tx_entry_t entries[APK_MAX_TX_LOG_ENTRIES];
    registers_t start_regs;
    u8  active;
} apk_tx_journal_t;

int  apk_tx_begin(u32 domain_id, const registers_t* regs);
int  apk_tx_log_write(u64 addr, u64 orig_val, u8 size);
int  apk_tx_commit(void);
int  apk_tx_rollback(registers_t* out_regs);

/* ── Micro-Domain Call Gate ── */
typedef int (*apk_domain_fn_t)(void* arg);
int  apk_call_domain(u32 domain_id, apk_domain_fn_t fn, void* arg);

/* ── Universal Polymorphic Multi-OS Binary Loader ── */
typedef enum {
    APK_BIN_UNKNOWN = 0,
    APK_BIN_LINUX_ELF,
    APK_BIN_WINDOWS_PE,
    APK_BIN_WASM,
    APK_BIN_SOVEREIGN_NATIVE
} apk_bin_type_t;

apk_bin_type_t apk_detect_binary_type(const u8* data, u32 size);
const char*    apk_binary_type_name(apk_bin_type_t type);
int            apk_polymorphic_exec(const char* path, const u8* data, u32 size, int argc, const char** argv);

#ifdef __cplusplus
}
#endif

#endif /* APK_H */
