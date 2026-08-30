#ifndef APK_IOMMU_H
#define APK_IOMMU_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IOMMU_VENDOR_NONE  0
#define IOMMU_VENDOR_INTEL 1 /* VT-d */
#define IOMMU_VENDOR_AMD   2 /* AMD-Vi */

typedef struct {
    u8  vendor;
    u8  is_enabled;
    u64 register_base_phys;
    u32 domain_count;
    u32 total_dma_retranslations;
} apk_iommu_state_t;

int  apk_iommu_init(void);
int  apk_iommu_map_device(u8 bus, u8 dev, u8 func, u32 domain_id);
int  apk_iommu_is_enabled(void);
void apk_iommu_get_state(apk_iommu_state_t* out_state);

#ifdef __cplusplus
}
#endif

#endif /* APK_IOMMU_H */
