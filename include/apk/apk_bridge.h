#ifndef APK_BRIDGE_H
#define APK_BRIDGE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*apk_driver_fn_t)(void* ctx, void* arg);

int apk_bridge_init(void);
int apk_bridge_call_storage(apk_driver_fn_t fn, void* ctx, void* arg);
int apk_bridge_call_network(apk_driver_fn_t fn, void* ctx, void* arg);
int apk_bridge_call_graphics(apk_driver_fn_t fn, void* ctx, void* arg);

#ifdef __cplusplus
}
#endif

#endif /* APK_BRIDGE_H */
