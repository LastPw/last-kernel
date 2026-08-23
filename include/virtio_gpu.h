#ifndef VIRTIO_GPU_H
#define VIRTIO_GPU_H

#include "types.h"

/* ── VirtIO-GPU Device IDs ── */
#define VIRTIO_GPU_DEVICE_ID_LEGACY    0x1040u
#define VIRTIO_GPU_DEVICE_ID_MODERN    0x1050u

/* ── Control Commands ── */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO       0x0100u
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D     0x0101u
#define VIRTIO_GPU_CMD_RESOURCE_UNREF         0x0102u
#define VIRTIO_GPU_CMD_SET_SCANOUT            0x0103u
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH         0x0104u
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D    0x0105u
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106u
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107u
#define VIRTIO_GPU_CMD_GET_EDID               0x010Au

/* ── Response Status ── */
#define VIRTIO_GPU_RESP_OK_NODATA          0x0110u
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO    0x0111u
#define VIRTIO_GPU_RESP_ERR_UNSPEC         0x0120u
#define VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY  0x0121u
#define VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID    0x0122u
#define VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID   0x0123u
#define VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER     0x0125u

/* ── Feature Bits ── */
#define VIRTIO_GPU_F_VIRGL    10u
#define VIRTIO_GPU_F_EDID     11u

/* ── Pixel Formats ── */
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM  1u

/* ── Scanout IDs ── */
#define VIRTIO_GPU_SCANOUT_ID_PRIMARY  0u

/* ── Control Header ── */
typedef struct __attribute__((packed)) {
    u32 type;
    u32 flags;
    u64 fence_id;
    u32 ctx_id;
    u32 padding;
} virtio_gpu_ctrl_hdr_t;

/* ── Rectangle ── */
typedef struct __attribute__((packed)) {
    u32 x;
    u32 y;
    u32 width;
    u32 height;
} virtio_gpu_rect_t;

/* ── Display Info Response ── */
typedef struct __attribute__((packed)) {
    u32 enabled;
    u32 x_offset;
    u32 y_offset;
    u32 width;
    u32 height;
    u32 flags;
} virtio_gpu_display_one_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_display_one_t displays[16];
} virtio_gpu_resp_display_info_t;

/* ── Resource Create 2D ── */
typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    u32 resource_id;
    u32 format;
    u32 width;
    u32 height;
} virtio_gpu_resource_create_2d_t;

/* ── Resource Attach Backing ── */
typedef struct __attribute__((packed)) {
    u64 addr;
} virtio_gpu_mem_entry_t;

typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    u32 resource_id;
    u32 nr_entries;
    /* virtio_gpu_mem_entry_t entries[nr_entries] follow */
} virtio_gpu_resource_attach_backing_t;

/* ── Set Scanout ── */
typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t rect;
    u32 resource_id;
    u32 scanout_id;
} virtio_gpu_set_scanout_t;

/* ── Transfer To Host 2D ── */
typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t rect;
    u32 offset;
    u32 resource_id;
    u32 padding;
} virtio_gpu_transfer_to_host_2d_t;

/* ── Resource Flush ── */
typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t rect;
    u32 resource_id;
    u32 padding;
} virtio_gpu_resource_flush_t;

/* ── Public API ── */

/* Initialize VirtIO-GPU: probe PCI, set up control queue, query display,
   create framebuffer resources, and set up scanout. Returns 0 on success. */
int virtio_gpu_init(void);

/* Returns 1 if VirtIO-GPU is initialized and ready. */
int virtio_gpu_available(void);

/* Display dimensions. */
u32 virtio_gpu_width(void);
u32 virtio_gpu_height(void);

/* Returns 1 if 3D virgl is supported. */
int virtio_gpu_has_3d(void);

/* Framebuffer info. Pitch = bytes per row. BPP = bytes per pixel (always 4). */
u32 virtio_gpu_framebuffer_pitch(void);
u32 virtio_gpu_framebuffer_bpp(void);
u32 virtio_gpu_framebuffer_size(void);

/* CPU-writable pointer to the back buffer (the buffer the CPU renders into).
   After writing, call virtio_gpu_flush_rect() or virtio_gpu_page_flip(). */
void* virtio_gpu_back_buffer(void);

/* Flush a rectangular region from the back buffer to the display.
   This does TRANSFER_TO_HOST_2D + RESOURCE_FLUSH on the back buffer resource.
   Use this for incremental updates without page flip. */
int virtio_gpu_flush_rect(u32 x, u32 y, u32 w, u32 h);

/* Page flip: swap the front and back buffers so the back buffer becomes
   the new scanout target. This is tearing-free when done between vblanks.
   After the flip, the CPU should write to the new back buffer (same pointer
   returned by virtio_gpu_back_buffer()). */
int virtio_gpu_page_flip(void);

/* Backend name string. */
const char* virtio_gpu_backend_name(void);

#endif /* VIRTIO_GPU_H */
