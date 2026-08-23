#ifndef I915_H
#define I915_H

#include "types.h"

#define INTEL_PCI_VENDOR_ID 0x8086

void i915_init(void);
int i915_available(void);
int i915_set_mode(u32 width, u32 height, u32 bpp);

#endif /* I915_H */
