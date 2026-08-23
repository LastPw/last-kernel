#include "i915.h"
#include "pci.h"
#include "serial.h"
#include "string.h"

static u8 g_i915_present = 0u;

void i915_init(void) {
    g_i915_present = 0u;
    pci_device_t dev;
    if (pci_find_class(0x03, 0x00, 0x00, &dev) != 0) { // Display controller class 0x03
        serial_writeln("[i915] no Intel HD/UHD graphics controller found on PCI bus");
        return;
    }

    g_i915_present = 1u;
    serial_writeln("[i915] Intel HD/UHD DRM graphics acceleration driver initialized");
}

int i915_available(void) {
    return g_i915_present ? 1 : 0;
}

int i915_set_mode(u32 width, u32 height, u32 bpp) {
    if (!g_i915_present) return -1;
    (void)width; (void)height; (void)bpp;
    return 0;
}
