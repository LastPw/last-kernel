#ifndef GPU_DRM_H
#define GPU_DRM_H

#include "types.h"
#include "pci.h"

typedef enum {
    GPU_VENDOR_UNKNOWN = 0,
    GPU_VENDOR_INTEL = 0x8086,
    GPU_VENDOR_NVIDIA = 0x10DE,
    GPU_VENDOR_AMD = 0x1002,
    GPU_VENDOR_VIRTIO = 0x1AF4
} gpu_vendor_t;

typedef struct {
    pci_device_t pci_dev;
    gpu_vendor_t vendor;
    u32 device_id;
    u64 mmio_base;
    u64 vram_base;
    u64 vram_size_mb;
    u32 display_w;
    u32 display_h;
    u32 bpp;
    u8 hw_accel_active;
    u8 dual_monitor_active;
} gpu_drm_device_t;

int gpu_drm_init(void);
int gpu_drm_find_device(gpu_drm_device_t* out_dev);
int gpu_drm_submit_command_buffer(gpu_drm_device_t* dev, const void* cmd_buf, u32 size);
int gpu_drm_vsync_flip(gpu_drm_device_t* dev, u64 fb_addr);

#endif
