#ifndef APK_TX_H
#define APK_TX_H

#include "types.h"
#include "idt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APK_TX_MAX_ENTRIES 512
#define APK_TX_MAX_PAGES   32

typedef struct {
    u64 addr;
    u64 original_val;
    u8  size; /* 1, 2, 4, 8 bytes */
} apk_tx_mutation_t;

typedef struct {
    u64 page_vaddr;
    u8  shadow_copy[4096];
    u32 original_crc32;
    u8  is_dirty;
} apk_tx_shadow_page_t;

typedef struct {
    u32 tx_id;
    u32 domain_id;
    u8  active;
    u8  is_committed;
    u32 mutation_count;
    apk_tx_mutation_t mutations[APK_TX_MAX_ENTRIES];
    u32 shadow_page_count;
    apk_tx_shadow_page_t shadow_pages[APK_TX_MAX_PAGES];
    registers_t start_registers;
    u32 start_tick;
} apk_tx_handle_t;

int  apk_tx_init(void);
int  apk_tx_start(u32 domain_id, const registers_t* regs, apk_tx_handle_t** out_tx);
int  apk_tx_record_write8(u64 addr, u8 orig_val);
int  apk_tx_record_write16(u64 addr, u16 orig_val);
int  apk_tx_record_write32(u64 addr, u32 orig_val);
int  apk_tx_record_write64(u64 addr, u64 orig_val);
int  apk_tx_record_page(u64 page_vaddr);

int  apk_tx_commit_active(void);
int  apk_tx_rollback_active(registers_t* out_regs);

u32  apk_tx_calc_crc32(const u8* data, u32 len);

#ifdef __cplusplus
}
#endif

#endif /* APK_TX_H */
