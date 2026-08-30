#include "apk_bridge.h"
#include "apk_core.h"
#include "apk_tx.h"
#include "apk_guard.h"
#include "serial.h"
#include "spinlock.h"

/*
 * APK Universal Driver Call Gate Sandboxing Bridge
 *
 * Intercepts calls to Storage, Network, and Graphics device drivers,
 * executing them inside an isolated transaction with PKEY enforcement.
 */

static spinlock_t s_bridge_lock;
static u8 s_bridge_inited = 0;

int apk_bridge_init(void) {
    if (s_bridge_inited) return 0;
    spinlock_init(&s_bridge_lock);
    s_bridge_inited = 1;
    serial_write("[APK_BRIDGE] Universal Driver Call Gate Sandboxing Bridge Active\n");
    return 0;
}

int apk_bridge_call_storage(apk_driver_fn_t fn, void* ctx, void* arg) {
    if (!fn) return -1;
    if (!s_bridge_inited) apk_bridge_init();

    if (apk_guard_is_quarantined(APK_DOMAIN_STORAGE)) {
        serial_write("[APK_BRIDGE] Storage Domain is Quarantined! Call rejected for safety.\n");
        return -1;
    }

    u32 prev = apk_core_get_current_domain();
    apk_core_set_current_domain(APK_DOMAIN_STORAGE);

    apk_tx_handle_t* tx = 0;
    apk_tx_start(APK_DOMAIN_STORAGE, 0, &tx);

    int res = fn(ctx, arg);

    apk_tx_commit_active();
    apk_core_set_current_domain(prev);
    return res;
}

int apk_bridge_call_network(apk_driver_fn_t fn, void* ctx, void* arg) {
    if (!fn) return -1;
    if (!s_bridge_inited) apk_bridge_init();

    if (apk_guard_is_quarantined(APK_DOMAIN_NET)) {
        serial_write("[APK_BRIDGE] Network Domain is Quarantined! Packet dropped for safety.\n");
        return -1;
    }

    u32 prev = apk_core_get_current_domain();
    apk_core_set_current_domain(APK_DOMAIN_NET);

    apk_tx_handle_t* tx = 0;
    apk_tx_start(APK_DOMAIN_NET, 0, &tx);

    int res = fn(ctx, arg);

    apk_tx_commit_active();
    apk_core_set_current_domain(prev);
    return res;
}

int apk_bridge_call_graphics(apk_driver_fn_t fn, void* ctx, void* arg) {
    if (!fn) return -1;
    if (!s_bridge_inited) apk_bridge_init();

    if (apk_guard_is_quarantined(APK_DOMAIN_GFX)) {
        serial_write("[APK_BRIDGE] Graphics Domain is Quarantined! Draw call skipped.\n");
        return -1;
    }

    u32 prev = apk_core_get_current_domain();
    apk_core_set_current_domain(APK_DOMAIN_GFX);

    apk_tx_handle_t* tx = 0;
    apk_tx_start(APK_DOMAIN_GFX, 0, &tx);

    int res = fn(ctx, arg);

    apk_tx_commit_active();
    apk_core_set_current_domain(prev);
    return res;
}
