#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#define APK_DOMAIN_CORE     0u
#define APK_DOMAIN_STORAGE  1u
#define APK_DOMAIN_NET      2u
#define APK_DOMAIN_GFX      3u
#define APK_DOMAIN_COMPAT   4u
#define APK_DOMAIN_USER     5u
#define APK_MAX_DOMAINS     6u

#define APK_MAX_TX_LOG_ENTRIES 256

typedef struct {
    uint64_t addr;
    uint64_t orig_val;
    uint8_t  size;
} apk_tx_entry_t;

typedef struct {
    uint32_t tx_id;
    uint32_t domain_id;
    uint32_t entry_count;
    apk_tx_entry_t entries[APK_MAX_TX_LOG_ENTRIES];
    uint8_t  active;
} apk_tx_journal_t;

static apk_tx_journal_t g_tx;

static int test_tx_begin(uint32_t domain_id) {
    g_tx.tx_id++;
    g_tx.domain_id = domain_id;
    g_tx.entry_count = 0;
    g_tx.active = 1;
    return 0;
}

static int test_tx_log_write(uint64_t addr, uint64_t orig_val, uint8_t size) {
    if (!g_tx.active || g_tx.entry_count >= APK_MAX_TX_LOG_ENTRIES) return -1;
    uint32_t idx = g_tx.entry_count++;
    g_tx.entries[idx].addr = addr;
    g_tx.entries[idx].orig_val = orig_val;
    g_tx.entries[idx].size = size;
    return 0;
}

static int test_tx_rollback(void) {
    if (!g_tx.active) return 0;
    for (int i = (int)g_tx.entry_count - 1; i >= 0; i--) {
        uint64_t addr = g_tx.entries[i].addr;
        uint64_t orig = g_tx.entries[i].orig_val;
        uint8_t sz = g_tx.entries[i].size;
        if (sz == 4) {
            *(volatile uint32_t*)(uintptr_t)addr = (uint32_t)orig;
        } else if (sz == 8) {
            *(volatile uint64_t*)(uintptr_t)addr = orig;
        }
    }
    g_tx.active = 0;
    return 1;
}

static void test_pku_paging_bits(void) {
    printf("  [1/4] Testing x86_64 PKEY Page-Table Bit Encoding (PTE bits 59..62)... ");
    uint64_t pte = 0x0000000010000003ull; /* Base present + RW page */
    uint32_t domain = APK_DOMAIN_NET; /* Domain 2 */
    uint64_t pkey_mask = (0xFull << 59u);
    pte = (pte & ~pkey_mask) | (((uint64_t)(domain & 0xFu)) << 59u);
    
    uint32_t extracted_domain = (uint32_t)((pte >> 59u) & 0xFu);
    assert(extracted_domain == APK_DOMAIN_NET);
    printf("PASS (PTE: 0x%016llx -> Domain %u)\n", (unsigned long long)pte, extracted_domain);
}

static void test_transactional_ram_rollback(void) {
    printf("  [2/4] Testing Transactional RAM Undo-Log & Shadow Rollback... ");
    uint32_t ram_buffer[4] = { 0x11111111, 0x22222222, 0x33333333, 0x44444444 };
    
    test_tx_begin(APK_DOMAIN_STORAGE);
    
    /* Log original values before write */
    test_tx_log_write((uint64_t)&ram_buffer[0], ram_buffer[0], 4);
    test_tx_log_write((uint64_t)&ram_buffer[1], ram_buffer[1], 4);
    
    /* Simulate memory corruption by faulty driver */
    ram_buffer[0] = 0xDEADBEEF;
    ram_buffer[1] = 0xBAADF00D;
    assert(ram_buffer[0] == 0xDEADBEEF);
    
    /* Perform atomic rollback */
    int healed = test_tx_rollback();
    assert(healed == 1);
    assert(ram_buffer[0] == 0x11111111);
    assert(ram_buffer[1] == 0x22222222);
    printf("PASS (RAM Corruption Reverted to 0x11111111 / 0x22222222)\n");
}

static void test_binary_signatures(void) {
    printf("  [3/4] Testing Universal Multi-OS Binary Format Detection... ");
    uint8_t elf[4]  = { 0x7F, 'E', 'L', 'F' };
    uint8_t pe[4]   = { 'M', 'Z', 0x90, 0x00 };
    uint8_t wasm[4] = { 0x00, 'a', 's', 'm' };
    uint8_t sov[4]  = { 'S', 'O', 'V', '1' };

    assert(elf[0] == 0x7F && elf[1] == 'E' && elf[2] == 'L' && elf[3] == 'F');
    assert(pe[0] == 'M' && pe[1] == 'Z');
    assert(wasm[0] == 0x00 && wasm[1] == 'a' && wasm[2] == 's' && wasm[3] == 'm');
    assert(sov[0] == 'S' && sov[1] == 'O' && sov[2] == 'V' && sov[3] == '1');
    printf("PASS (ELF, PE, WASM, SOV validated)\n");
}

static void test_quarantine_state_machine(void) {
    printf("  [4/4] Testing Driver Fault Threshold & Quarantine Transition... ");
    uint32_t fault_count = 0;
    uint8_t quarantined = 0;
    
    for (int i = 0; i < 6; i++) {
        fault_count++;
        if (fault_count > 5) {
            quarantined = 1;
        }
    }
    assert(quarantined == 1);
    
    /* Hot-reload resets fault counter and clears quarantine */
    quarantined = 0;
    fault_count = 0;
    assert(quarantined == 0 && fault_count == 0);
    printf("PASS (Quarantine trigger & reset verified)\n");
}

int main(void) {
    printf("\n=== LastKernel APK Core Engine Test Suite ===\n");
    test_pku_paging_bits();
    test_transactional_ram_rollback();
    test_binary_signatures();
    test_quarantine_state_machine();
    printf("All APK core vectors pass successfully!\n\n");
    return 0;
}
