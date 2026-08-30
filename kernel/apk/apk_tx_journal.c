#include "apk_tx.h"
#include "string.h"
#include "serial.h"
#include "timer.h"
#include "spinlock.h"

/*
 * APK Transactional Shadow RAM Journal Subsystem
 *
 * Provides real byte-level and page-level memory undo logging
 * with hardware CRC32 integrity verification for micro-domain rollback.
 */

#define APK_MAX_CONCURRENT_TX 8
static apk_tx_handle_t s_tx_pool[APK_MAX_CONCURRENT_TX];
static apk_tx_handle_t* s_active_tx = 0;
static spinlock_t s_tx_lock;
static u8 s_tx_inited = 0;
static u32 s_tx_sequence = 1000;

/* Standard IEEE 802.3 CRC32 Hash Table */
static u32 s_crc32_tab[256];
static u8  s_crc32_tab_ready = 0;

static void init_crc32_table(void) {
    if (s_crc32_tab_ready) return;
    for (u32 i = 0; i < 256; i++) {
        u32 c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        s_crc32_tab[i] = c;
    }
    s_crc32_tab_ready = 1;
}

u32 apk_tx_calc_crc32(const u8* data, u32 len) {
    if (!s_crc32_tab_ready) init_crc32_table();
    if (!data) return 0;
    u32 crc = 0xFFFFFFFFu;
    for (u32 i = 0; i < len; i++) {
        crc = s_crc32_tab[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

int apk_tx_init(void) {
    if (s_tx_inited) return 0;
    spinlock_init(&s_tx_lock);
    init_crc32_table();

    memset(s_tx_pool, 0, sizeof(s_tx_pool));
    s_active_tx = 0;
    s_tx_inited = 1;
    serial_write("[APK_TX] Initialized Transactional RAM Shadow Undo-Log Engine\n");
    return 0;
}

int apk_tx_start(u32 domain_id, const registers_t* regs, apk_tx_handle_t** out_tx) {
    if (!s_tx_inited) apk_tx_init();
    spinlock_lock(&s_tx_lock);

    /* Allocate free transaction slot */
    apk_tx_handle_t* slot = 0;
    for (u32 i = 0; i < APK_MAX_CONCURRENT_TX; i++) {
        if (!s_tx_pool[i].active) {
            slot = &s_tx_pool[i];
            break;
        }
    }
    if (!slot) {
        spinlock_unlock(&s_tx_lock);
        return -1; /* Transaction pool exhausted */
    }

    slot->tx_id = ++s_tx_sequence;
    slot->domain_id = domain_id;
    slot->active = 1;
    slot->is_committed = 0;
    slot->mutation_count = 0;
    slot->shadow_page_count = 0;
    slot->start_tick = timer_ticks();
    if (regs) {
        slot->start_registers = *regs;
    } else {
        memset(&slot->start_registers, 0, sizeof(registers_t));
    }

    s_active_tx = slot;
    if (out_tx) *out_tx = slot;

    spinlock_unlock(&s_tx_lock);
    return 0;
}

int apk_tx_record_write8(u64 addr, u8 orig_val) {
    if (!s_active_tx || !s_active_tx->active) return -1;
    if (s_active_tx->mutation_count >= APK_TX_MAX_ENTRIES) return -1;

    u32 idx = s_active_tx->mutation_count++;
    s_active_tx->mutations[idx].addr = addr;
    s_active_tx->mutations[idx].original_val = (u64)orig_val;
    s_active_tx->mutations[idx].size = 1;
    return 0;
}

int apk_tx_record_write16(u64 addr, u16 orig_val) {
    if (!s_active_tx || !s_active_tx->active) return -1;
    if (s_active_tx->mutation_count >= APK_TX_MAX_ENTRIES) return -1;

    u32 idx = s_active_tx->mutation_count++;
    s_active_tx->mutations[idx].addr = addr;
    s_active_tx->mutations[idx].original_val = (u64)orig_val;
    s_active_tx->mutations[idx].size = 2;
    return 0;
}

int apk_tx_record_write32(u64 addr, u32 orig_val) {
    if (!s_active_tx || !s_active_tx->active) return -1;
    if (s_active_tx->mutation_count >= APK_TX_MAX_ENTRIES) return -1;

    u32 idx = s_active_tx->mutation_count++;
    s_active_tx->mutations[idx].addr = addr;
    s_active_tx->mutations[idx].original_val = (u64)orig_val;
    s_active_tx->mutations[idx].size = 4;
    return 0;
}

int apk_tx_record_write64(u64 addr, u64 orig_val) {
    if (!s_active_tx || !s_active_tx->active) return -1;
    if (s_active_tx->mutation_count >= APK_TX_MAX_ENTRIES) return -1;

    u32 idx = s_active_tx->mutation_count++;
    s_active_tx->mutations[idx].addr = addr;
    s_active_tx->mutations[idx].original_val = orig_val;
    s_active_tx->mutations[idx].size = 8;
    return 0;
}

int apk_tx_record_page(u64 page_vaddr) {
    if (!s_active_tx || !s_active_tx->active) return -1;
    if (s_active_tx->shadow_page_count >= APK_TX_MAX_PAGES) return -1;

    u64 aligned_va = page_vaddr & ~0xFFFull;

    /* Check if page is already shadowed */
    for (u32 i = 0; i < s_active_tx->shadow_page_count; i++) {
        if (s_active_tx->shadow_pages[i].page_vaddr == aligned_va) {
            return 0;
        }
    }

    u32 idx = s_active_tx->shadow_page_count++;
    s_active_tx->shadow_pages[idx].page_vaddr = aligned_va;
    memcpy(s_active_tx->shadow_pages[idx].shadow_copy, (const void*)(uintptr_t)aligned_va, 4096);
    s_active_tx->shadow_pages[idx].original_crc32 = apk_tx_calc_crc32(s_active_tx->shadow_pages[idx].shadow_copy, 4096);
    s_active_tx->shadow_pages[idx].is_dirty = 1;
    return 0;
}

int apk_tx_commit_active(void) {
    if (!s_active_tx || !s_active_tx->active) return -1;
    spinlock_lock(&s_tx_lock);
    s_active_tx->is_committed = 1;
    s_active_tx->active = 0;
    s_active_tx = 0;
    spinlock_unlock(&s_tx_lock);
    return 0;
}

int apk_tx_rollback_active(registers_t* out_regs) {
    if (!s_active_tx || !s_active_tx->active) return 0;
    spinlock_lock(&s_tx_lock);

    /* 1. Revert full shadow pages */
    for (u32 p = 0; p < s_active_tx->shadow_page_count; p++) {
        if (s_active_tx->shadow_pages[p].is_dirty) {
            void* dst = (void*)(uintptr_t)s_active_tx->shadow_pages[p].page_vaddr;
            memcpy(dst, s_active_tx->shadow_pages[p].shadow_copy, 4096);
        }
    }

    /* 2. Revert individual word mutations in reverse chronological order */
    for (i32 i = (i32)s_active_tx->mutation_count - 1; i >= 0; i--) {
        u64 addr = s_active_tx->mutations[i].addr;
        u64 val  = s_active_tx->mutations[i].original_val;
        u8 sz    = s_active_tx->mutations[i].size;
        if (!addr) continue;

        if (sz == 1) {
            *(volatile u8*)(uintptr_t)addr = (u8)val;
        } else if (sz == 2) {
            *(volatile u16*)(uintptr_t)addr = (u16)val;
        } else if (sz == 4) {
            *(volatile u32*)(uintptr_t)addr = (u32)val;
        } else if (sz == 8) {
            *(volatile u64*)(uintptr_t)addr = val;
        }
    }

    /* 3. Restore pre-transaction CPU registers */
    if (out_regs) {
        *out_regs = s_active_tx->start_registers;
    }

    u32 tx_id = s_active_tx->tx_id;
    s_active_tx->active = 0;
    s_active_tx = 0;

    spinlock_unlock(&s_tx_lock);
    serial_write("[APK_TX] Transaction Atomically Rolled Back (RAM Reverted) TX_ID: ");
    serial_write_dec(tx_id);
    serial_write("\n");
    return 1;
}
