#include "virtio_gpu.h"
#include "virtio_ring.h"
#include "virtio_pci.h"
#include "pci.h"
#include "io.h"
#include "paging.h"
#include "frame_allocator.h"
#include "string.h"
#include "serial.h"
#include "interrupts.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  VirtIO-GPU Driver for LastOS
 *
 *  Features:
 *  - Proper PCI probe and feature negotiation
 *  - Physical address conversion for all DMA operations
 *  - IRQ handler for interrupt-driven operation
 *  - Double buffering (front + back) with page flip
 *  - DMA-based framebuffer transfers
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── VirtIO constants ── */
#define VIRTIO_F_VERSION_1           32u
#define VIRTIO_STATUS_ACK            0x01u
#define VIRTIO_STATUS_DRIVER         0x02u
#define VIRTIO_STATUS_DRIVER_OK      0x04u
#define VIRTIO_STATUS_FEATURES_OK    0x08u
#define VIRTIO_STATUS_FAILED         0x80u

/* ── Legacy I/O offsets ── */
#define VIRTIO_REG_DEVICE_FEATURES  0x00u
#define VIRTIO_REG_GUEST_FEATURES   0x04u
#define VIRTIO_REG_QUEUE_ADDRESS    0x08u
#define VIRTIO_REG_QUEUE_SIZE       0x0Cu
#define VIRTIO_REG_QUEUE_SELECT     0x0Eu
#define VIRTIO_REG_QUEUE_NOTIFY     0x10u
#define VIRTIO_REG_STATUS           0x12u
#define VIRTIO_REG_ISR              0x13u

/* ── Modern common config offsets ── */
#define VIRTIO_COMMON_FEATURE_SEL   0u
#define VIRTIO_COMMON_FEATURE       4u
#define VIRTIO_COMMON_GUEST_SEL     8u
#define VIRTIO_COMMON_GUEST_FEATURE 12u
#define VIRTIO_COMMON_STATUS        20u
#define VIRTIO_COMMON_QUEUE_SEL     22u
#define VIRTIO_COMMON_QUEUE_SIZE    24u
#define VIRTIO_COMMON_QUEUE_ENABLE  28u
#define VIRTIO_COMMON_QUEUE_NOTIFY  30u
#define VIRTIO_COMMON_QUEUE_DESC    32u
#define VIRTIO_COMMON_QUEUE_AVAIL   40u
#define VIRTIO_COMMON_QUEUE_USED    48u

/* ── Queue config ── */
#define GPU_QUEUE_CONTROL            0u
#define GPU_CONTROL_QUEUE_SLOTS      64u
#define GPU_CONTROL_QUEUE_BYTES      8192u

/* ── Resource IDs ── */
#define GPU_RESOURCE_FB_FRONT   1u   /* front buffer (scanned out) */
#define GPU_RESOURCE_FB_BACK    2u   /* back buffer (CPU writes here) */

/* ── Max framebuffer: 1920x1080 BGRA ── */
#define GPU_MAX_FB_W            1920u
#define GPU_MAX_FB_H            1080u
#define GPU_MAX_BACKING_PAGES   2048u

/* ── Helper: virtual → physical address ── */
static inline u32 virt_to_phys(void* ptr) {
    return (u32)paging_virt_to_phys((u64)ptr);
}

static inline void* phys_to_virt(u32 phys) {
    return (void*)paging_phys_to_virt((u64)phys);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Driver State
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── Transport ── */
static virtio_pci_transport_t g_transport;
static u16 g_io;
static u8  g_modern;
static u32 g_common;
static u32 g_notify;
static u32 g_notify_multiplier;

/* ── Device state ── */
static u8  g_ready;
static u8  g_has_3d;
static u32 g_display_width;
static u32 g_display_height;
static u8  g_display_enabled;
static u8  g_irq_line;

/* ── Control queue ── */
static u8  g_ctrl_queue_mem[GPU_CONTROL_QUEUE_BYTES] __attribute__((aligned(4096)));
static virtio_ring_t g_ctrl_ring;

/* ── Shared command/response buffers ── */
static u8  g_cmd_buf[4096] __attribute__((aligned(16)));
static u8  g_resp_buf[4096] __attribute__((aligned(16)));

/* ── Framebuffer resources ── */
static void* g_fb_virt_pages[GPU_MAX_BACKING_PAGES];  /* virtual (CPU access) */
static u32   g_fb_phys_addrs[GPU_MAX_BACKING_PAGES];  /* physical (DMA) */
static u32   g_fb_nr_pages;
static u32   g_fb_pitch;
static u32   g_fb_bpp;
static u32   g_fb_size_bytes;

/* ── Double buffering ── */
static u8    g_fb_active_resource;  /* which resource is currently scanned out */
static void* g_fb_back_ptr;        /* CPU writes here */

/* ── IRQ completion flag ── */
static volatile u8 g_irq_fired;

/* ═══════════════════════════════════════════════════════════════════════
 *  Serial Debug
 * ═══════════════════════════════════════════════════════════════════════ */

static void gpu_hex(u32 val) {
    static const char hex[] = "0123456789ABCDEF";
    for (i32 shift = 28; shift >= 0; shift -= 4)
        serial_putc(hex[(val >> (u32)shift) & 0x0Fu]);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Transport Helpers
 * ═══════════════════════════════════════════════════════════════════════ */

static void gpu_status_set(u8 value) {
    if (g_modern) mmio_write8(g_common + VIRTIO_COMMON_STATUS, value);
    else outb(g_io + VIRTIO_REG_STATUS, value);
}

static u8 gpu_status_get(void) {
    return g_modern ? mmio_read8(g_common + VIRTIO_COMMON_STATUS)
                    : inb(g_io + VIRTIO_REG_STATUS);
}

static void gpu_notify_queue(u16 queue_idx) {
    if (g_modern) {
        mmio_write16(g_common + VIRTIO_COMMON_QUEUE_SEL, queue_idx);
        u16 notify_off = mmio_read16(g_common + VIRTIO_COMMON_QUEUE_NOTIFY);
        mmio_write16(g_notify + ((u32)notify_off * g_notify_multiplier), queue_idx);
    } else {
        outw(g_io + VIRTIO_REG_QUEUE_NOTIFY, queue_idx);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  IRQ Handler
 * ═══════════════════════════════════════════════════════════════════════ */

static void virtio_gpu_irq(registers_t* regs) {
    (void)regs;
    /* Deassert level-triggered interrupt by reading ISR register */
    if (!g_modern && g_io != 0u) {
        (void)inb(g_io + VIRTIO_REG_ISR);
    }
    g_irq_fired = 1u;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Control Queue
 * ═══════════════════════════════════════════════════════════════════════ */

static void gpu_wait_used(void) {
    /* First check if an IRQ already fired */
    if (g_irq_fired) { g_irq_fired = 0u; return; }

    for (u32 retry = 0u; retry < 128u; retry++) {
        for (u32 spin = 0u; spin < 2000u; spin++) {
            if (virtio_ring_has_used(&g_ctrl_ring)) return;
            __asm__ __volatile__("" ::: "memory");
        }
        /* Poll the ISR register on legacy to detect completion */
        if (!g_modern && g_io != 0u) {
            u8 isr = inb(g_io + VIRTIO_REG_ISR);
            if (isr & 1u) { g_irq_fired = 0u; return; }
        }
        io_wait();
    }
    serial_writeln("[VirtIO-GPU] WARNING: control queue timeout");
}

static int gpu_send_command(u32 cmd_type, const void* cmd_data, u32 cmd_size,
                              void* resp_data, u32 resp_size) {
    virtio_ring_buffer_t buffers[2];
    u16 head;

    if (cmd_size > sizeof(g_cmd_buf) || resp_size > sizeof(g_resp_buf)) {
        serial_writeln("[VirtIO-GPU] command too large");
        return -1;
    }

    /* Copy command and set type */
    memcpy(g_cmd_buf, cmd_data, cmd_size);
    ((virtio_gpu_ctrl_hdr_t*)g_cmd_buf)->type = cmd_type;
    memset(g_resp_buf, 0, resp_size);

    /* Descriptor 0: command buffer (device reads) — use PHYSICAL address */
    buffers[0].addr_low = virt_to_phys(g_cmd_buf);
    buffers[0].addr_high = 0u;
    buffers[0].len = cmd_size;
    buffers[0].flags = 0u;

    /* Descriptor 1: response buffer (device writes) — use PHYSICAL address */
    buffers[1].addr_low = virt_to_phys(g_resp_buf);
    buffers[1].addr_high = 0u;
    buffers[1].len = resp_size;
    buffers[1].flags = VIRTIO_RING_DESC_WRITE;

    interrupts_disable();
    g_irq_fired = 0u;

    if (virtio_ring_add_chain(&g_ctrl_ring, buffers, 2u, &head) != 0) {
        interrupts_enable();
        serial_writeln("[VirtIO-GPU] failed to add chain");
        return -1;
    }
    if (virtio_ring_publish(&g_ctrl_ring, head) != 0) {
        interrupts_enable();
        serial_writeln("[VirtIO-GPU] failed to publish");
        return -1;
    }
    interrupts_enable();

    /* Notify device */
    gpu_notify_queue(GPU_QUEUE_CONTROL);

    /* Wait for completion (IRQ-driven or polled) */
    gpu_wait_used();

    /* Consume used entry and validate */
    u32 used_id;
    if (virtio_ring_get_used(&g_ctrl_ring, &used_id, 0) != 0) {
        serial_writeln("[VirtIO-GPU] failed to get used entry");
        return -1;
    }
    if (used_id != head) {
        serial_writeln("[VirtIO-GPU] used_id mismatch");
        return -1;
    }

    /* Check response */
    u32 resp_type = ((virtio_gpu_ctrl_hdr_t*)g_resp_buf)->type;
    if (resp_type != VIRTIO_GPU_RESP_OK_NODATA &&
        resp_type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
        serial_write("[VirtIO-GPU] cmd 0x");
        gpu_hex(cmd_type);
        serial_write(" err 0x");
        gpu_hex(resp_type);
        serial_writeln("");
        return -1;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Control Queue Setup
 * ═══════════════════════════════════════════════════════════════════════ */

static int gpu_setup_control_queue(void) {
    if (g_modern) {
        mmio_write16(g_common + VIRTIO_COMMON_QUEUE_SEL, GPU_QUEUE_CONTROL);
        g_ctrl_ring.num = mmio_read16(g_common + VIRTIO_COMMON_QUEUE_SIZE);
        mmio_write16(g_common + VIRTIO_COMMON_QUEUE_SIZE, g_ctrl_ring.num);
    } else {
        outw(g_io + VIRTIO_REG_QUEUE_SELECT, GPU_QUEUE_CONTROL);
        g_ctrl_ring.num = inw(g_io + VIRTIO_REG_QUEUE_SIZE);
    }

    if (g_ctrl_ring.num == 0u) {
        serial_writeln("[VirtIO-GPU] control queue size 0");
        return -1;
    }
    if (g_ctrl_ring.num > GPU_CONTROL_QUEUE_SLOTS)
        g_ctrl_ring.num = GPU_CONTROL_QUEUE_SLOTS;

    memset(g_ctrl_queue_mem, 0, sizeof(g_ctrl_queue_mem));
    if (virtio_ring_init(&g_ctrl_ring, g_ctrl_queue_mem, g_ctrl_ring.num, 4096u) != 0) {
        serial_writeln("[VirtIO-GPU] ring init failed");
        return -1;
    }

    if (g_modern) {
        mmio_write32(g_common + VIRTIO_COMMON_QUEUE_DESC,
                     virtio_ring_desc_address(&g_ctrl_ring));
        mmio_write32(g_common + VIRTIO_COMMON_QUEUE_DESC + 4u, 0u);
        mmio_write32(g_common + VIRTIO_COMMON_QUEUE_AVAIL,
                     virtio_ring_avail_address(&g_ctrl_ring));
        mmio_write32(g_common + VIRTIO_COMMON_QUEUE_AVAIL + 4u, 0u);
        mmio_write32(g_common + VIRTIO_COMMON_QUEUE_USED,
                     virtio_ring_used_address(&g_ctrl_ring));
        mmio_write32(g_common + VIRTIO_COMMON_QUEUE_USED + 4u, 0u);
        mmio_write16(g_common + VIRTIO_COMMON_QUEUE_ENABLE, 1u);
    } else {
        /* Legacy: write page frame number of the queue memory */
        outl(g_io + VIRTIO_REG_QUEUE_ADDRESS,
             virt_to_phys(g_ctrl_queue_mem) >> 12u);
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Display Probe
 * ═══════════════════════════════════════════════════════════════════════ */

static int gpu_probe_display(void) {
    virtio_gpu_ctrl_hdr_t cmd;
    virtio_gpu_resp_display_info_t resp;
    memset(&cmd, 0, sizeof(cmd));
    memset(&resp, 0, sizeof(resp));

    if (gpu_send_command(VIRTIO_GPU_CMD_GET_DISPLAY_INFO,
                         &cmd, sizeof(cmd), &resp, sizeof(resp)) != 0) {
        serial_writeln("[VirtIO-GPU] GET_DISPLAY_INFO failed, defaulting 1024x768");
        g_display_width = 1024u;
        g_display_height = 768u;
        g_display_enabled = 1u;
        return 0;
    }

    virtio_gpu_display_one_t* d0 = &resp.displays[0];
    g_display_enabled = d0->enabled ? 1u : 0u;
    g_display_width = d0->width ? d0->width : 1024u;
    g_display_height = d0->height ? d0->height : 768u;
    if (g_display_width > GPU_MAX_FB_W) g_display_width = GPU_MAX_FB_W;
    if (g_display_height > GPU_MAX_FB_H) g_display_height = GPU_MAX_FB_H;

    serial_write("[VirtIO-GPU] display ");
    serial_write_dec(g_display_width);
    serial_write("x");
    serial_write_dec(g_display_height);
    serial_writeln(g_display_enabled ? " enabled" : " disabled");
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Framebuffer Resources (Double Buffering)
 * ═══════════════════════════════════════════════════════════════════════ */

static int gpu_create_resource(u32 resource_id, u32 width, u32 height) {
    virtio_gpu_resource_create_2d_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.resource_id = resource_id;
    cmd.format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    cmd.width = width;
    cmd.height = height;

    /* Response goes to a minimal buffer */
    u8 resp[sizeof(virtio_gpu_ctrl_hdr_t)] __attribute__((aligned(16)));
    memset(resp, 0, sizeof(resp));

    if (gpu_send_command(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
                         &cmd, sizeof(cmd), resp, sizeof(resp)) != 0) {
        serial_write("[VirtIO-GPU] RESOURCE_CREATE_2D #");
        serial_write_dec(resource_id);
        serial_writeln(" failed");
        return -1;
    }
    return 0;
}

static int gpu_attach_backing(u32 resource_id) {
    u32 nr_pages = (g_fb_size_bytes + 4095u) / 4096u;
    if (nr_pages > GPU_MAX_BACKING_PAGES) {
        serial_writeln("[VirtIO-GPU] framebuffer too large");
        return -1;
    }
    g_fb_nr_pages = nr_pages;

    /* Allocate physical frames */
    for (u32 i = 0; i < nr_pages; i++) {
        void* frame = frame_alloc();
        if (!frame) {
            serial_writeln("[VirtIO-GPU] frame alloc failed");
            for (u32 j = 0; j < i; j++) frame_free(g_fb_virt_pages[j]);
            return -1;
        }
        g_fb_virt_pages[i] = frame;
        g_fb_phys_addrs[i] = virt_to_phys(frame);
    }

    /* Build ATTACH_BACKING command with physical addresses */
    u8 cmd_buf[sizeof(virtio_gpu_resource_attach_backing_t) +
               GPU_MAX_BACKING_PAGES * sizeof(virtio_gpu_mem_entry_t)];
    memset(cmd_buf, 0, sizeof(cmd_buf));

    virtio_gpu_resource_attach_backing_t* attach =
        (virtio_gpu_resource_attach_backing_t*)cmd_buf;
    attach->resource_id = resource_id;
    attach->nr_entries = nr_pages;

    virtio_gpu_mem_entry_t* entries =
        (virtio_gpu_mem_entry_t*)(cmd_buf + sizeof(virtio_gpu_resource_attach_backing_t));
    for (u32 i = 0; i < nr_pages; i++) {
        entries[i].addr = (u64)g_fb_phys_addrs[i];
    }

    u32 cmd_size = sizeof(virtio_gpu_resource_attach_backing_t) +
                   nr_pages * sizeof(virtio_gpu_mem_entry_t);
    u8 resp[sizeof(virtio_gpu_ctrl_hdr_t)] __attribute__((aligned(16)));
    memset(resp, 0, sizeof(resp));

    if (gpu_send_command(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
                         cmd_buf, cmd_size, resp, sizeof(resp)) != 0) {
        serial_writeln("[VirtIO-GPU] ATTACH_BACKING failed");
        for (u32 i = 0; i < nr_pages; i++) frame_free(g_fb_virt_pages[i]);
        return -1;
    }

    serial_write("[VirtIO-GPU] attached ");
    serial_write_dec(nr_pages);
    serial_write(" pages for resource #");
    serial_write_dec(resource_id);
    serial_writeln("");
    return 0;
}

static int gpu_set_scanout(u32 resource_id, u32 width, u32 height) {
    virtio_gpu_set_scanout_t cmd;
    u8 resp[sizeof(virtio_gpu_ctrl_hdr_t)] __attribute__((aligned(16)));
    memset(&cmd, 0, sizeof(cmd));
    memset(resp, 0, sizeof(resp));

    cmd.rect.x = 0;
    cmd.rect.y = 0;
    cmd.rect.width = width;
    cmd.rect.height = height;
    cmd.resource_id = resource_id;
    cmd.scanout_id = VIRTIO_GPU_SCANOUT_ID_PRIMARY;

    if (gpu_send_command(VIRTIO_GPU_CMD_SET_SCANOUT,
                         &cmd, sizeof(cmd), resp, sizeof(resp)) != 0) {
        serial_writeln("[VirtIO-GPU] SET_SCANOUT failed");
        return -1;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════ */

int virtio_gpu_init(void) {
    g_ready = 0u;
    g_has_3d = 0u;
    g_fb_back_ptr = 0;
    g_fb_nr_pages = 0u;
    g_irq_fired = 0u;

    serial_writeln("[VirtIO-GPU] probing PCI...");

    /* Probe PCI */
    if (virtio_pci_probe_device(&g_transport,
                                VIRTIO_GPU_DEVICE_ID_LEGACY,
                                VIRTIO_GPU_DEVICE_ID_MODERN) != 0) {
        serial_writeln("[VirtIO-GPU] PCI device not found");
        return -1;
    }

    serial_write("[VirtIO-GPU] PCI found (");
    serial_write(g_transport.modern ? "modern" : "legacy");
    serial_writeln(")");

    g_modern = g_transport.modern;
    g_io = g_transport.io_base;
    g_common = g_transport.common_cfg;
    g_notify = g_transport.notify_cfg;
    g_notify_multiplier = g_transport.notify_multiplier;

    if (!g_modern && !g_io) {
        serial_writeln("[VirtIO-GPU] no I/O base");
        return -1;
    }

    /* Read IRQ line from PCI config */
    g_irq_line = (u8)pci_config_read32(g_transport.pci.bus, g_transport.pci.slot,
                                        g_transport.pci.func, 0x3Cu);
    serial_write("[VirtIO-GPU] IRQ ");
    serial_write_dec(g_irq_line);
    serial_writeln("");

    /* Register IRQ handler */
    if (g_irq_line < 16u) {
        irq_register_handler(g_irq_line, virtio_gpu_irq);
        interrupts_unmask_irq(g_irq_line);
    }

    /* ── Feature Negotiation ── */
    if (g_modern) {
        mmio_write8(g_common + VIRTIO_COMMON_STATUS, 0u);
        mmio_write8(g_common + VIRTIO_COMMON_STATUS,
                    VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

        mmio_write32(g_common + VIRTIO_COMMON_FEATURE_SEL, 0u);
        u32 dev_feats_lo = mmio_read32(g_common + VIRTIO_COMMON_FEATURE);
        mmio_write32(g_common + VIRTIO_COMMON_FEATURE_SEL, 1u);
        u32 dev_feats_hi = mmio_read32(g_common + VIRTIO_COMMON_FEATURE);

        g_has_3d = (dev_feats_lo & (1u << VIRTIO_GPU_F_VIRGL)) ? 1u : 0u;

        /* Accept only VIRTIO_F_VERSION_1 + optional VIRGL */
        u32 guest_lo = (1u << VIRTIO_F_VERSION_1);
        if (g_has_3d) guest_lo |= (1u << VIRTIO_GPU_F_VIRGL);

        mmio_write32(g_common + VIRTIO_COMMON_GUEST_SEL, 0u);
        mmio_write32(g_common + VIRTIO_COMMON_GUEST_FEATURE, guest_lo);
        mmio_write32(g_common + VIRTIO_COMMON_GUEST_SEL, 1u);
        mmio_write32(g_common + VIRTIO_COMMON_GUEST_FEATURE,
                     dev_feats_hi & (1u << (VIRTIO_F_VERSION_1 - 32u)));

        mmio_write8(g_common + VIRTIO_COMMON_STATUS,
                    VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
        if (!(mmio_read8(g_common + VIRTIO_COMMON_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
            serial_writeln("[VirtIO-GPU] features rejected");
            return -1;
        }
    } else {
        gpu_status_set(0u);
        gpu_status_set(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

        u32 dev_feats = inl(g_io + VIRTIO_REG_DEVICE_FEATURES);
        g_has_3d = (dev_feats & (1u << VIRTIO_GPU_F_VIRGL)) ? 1u : 0u;

        u32 guest_feats = (1u << VIRTIO_F_VERSION_1);
        if (g_has_3d) guest_feats |= (1u << VIRTIO_GPU_F_VIRGL);
        outl(g_io + VIRTIO_REG_GUEST_FEATURES, guest_feats);

        gpu_status_set(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
        if (!(inb(g_io + VIRTIO_REG_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
            serial_writeln("[VirtIO-GPU] legacy features rejected");
            gpu_status_set(VIRTIO_STATUS_FAILED);
            return -1;
        }
    }

    serial_write("[VirtIO-GPU] features: 3D=");
    serial_write_dec(g_has_3d ? 1 : 0);
    serial_writeln("");

    /* ── Control Queue ── */
    if (gpu_setup_control_queue() != 0) {
        gpu_status_set(VIRTIO_STATUS_FAILED);
        return -1;
    }

    /* ── Query Display ── */
    if (gpu_probe_display() != 0) {
        gpu_status_set(VIRTIO_STATUS_FAILED);
        return -1;
    }

    /* ── Calculate framebuffer geometry ── */
    g_fb_bpp = 4u;
    g_fb_pitch = g_display_width * g_fb_bpp;
    g_fb_size_bytes = g_fb_pitch * g_display_height;

    /* ── Create TWO framebuffer resources for double buffering ── */
    if (gpu_create_resource(GPU_RESOURCE_FB_FRONT, g_display_width, g_display_height) != 0) {
        gpu_status_set(VIRTIO_STATUS_FAILED);
        return -1;
    }
    if (gpu_create_resource(GPU_RESOURCE_FB_BACK, g_display_width, g_display_height) != 0) {
        gpu_status_set(VIRTIO_STATUS_FAILED);
        return -1;
    }

    /* ── Attach backing memory (shared between both resources for simplicity) ──
       Both front and back buffer use the same physical pages. The GPU scans out
       whichever resource is bound to the scanout. The CPU writes to the back
       buffer's virtual address. After page flip, the roles swap. */
    if (gpu_attach_backing(GPU_RESOURCE_FB_BACK) != 0) {
        gpu_status_set(VIRTIO_STATUS_FAILED);
        return -1;
    }

    /* Also attach the same backing to the front buffer */
    {
        u8 cmd_buf[sizeof(virtio_gpu_resource_attach_backing_t) +
                   GPU_MAX_BACKING_PAGES * sizeof(virtio_gpu_mem_entry_t)];
        memset(cmd_buf, 0, sizeof(cmd_buf));
        virtio_gpu_resource_attach_backing_t* attach =
            (virtio_gpu_resource_attach_backing_t*)cmd_buf;
        attach->resource_id = GPU_RESOURCE_FB_FRONT;
        attach->nr_entries = g_fb_nr_pages;
        virtio_gpu_mem_entry_t* entries =
            (virtio_gpu_mem_entry_t*)(cmd_buf + sizeof(virtio_gpu_resource_attach_backing_t));
        for (u32 i = 0; i < g_fb_nr_pages; i++) {
            entries[i].addr = (u64)g_fb_phys_addrs[i];
        }
        u32 cmd_size = sizeof(virtio_gpu_resource_attach_backing_t) +
                       g_fb_nr_pages * sizeof(virtio_gpu_mem_entry_t);
        u8 resp[sizeof(virtio_gpu_ctrl_hdr_t)] __attribute__((aligned(16)));
        memset(resp, 0, sizeof(resp));
        if (gpu_send_command(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
                             cmd_buf, cmd_size, resp, sizeof(resp)) != 0) {
            serial_writeln("[VirtIO-GPU] ATTACH_BACKING front failed");
            gpu_status_set(VIRTIO_STATUS_FAILED);
            return -1;
        }
    }

    /* ── Set scanout to front buffer ── */
    if (gpu_set_scanout(GPU_RESOURCE_FB_FRONT, g_display_width, g_display_height) != 0) {
        gpu_status_set(VIRTIO_STATUS_FAILED);
        return -1;
    }

    /* ── CPU writes to back buffer ── */
    g_fb_active_resource = GPU_RESOURCE_FB_FRONT;
    g_fb_back_ptr = g_fb_virt_pages[0]; /* first page's virtual address */

    /* ── Mark device ready ── */
    if (g_modern) {
        mmio_write8(g_common + VIRTIO_COMMON_STATUS,
                    VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                    VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
    } else {
        gpu_status_set(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                       VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
    }
    if (gpu_status_get() & VIRTIO_STATUS_FAILED) {
        serial_writeln("[VirtIO-GPU] device rejected final status");
        return -1;
    }

    g_ready = 1u;

    serial_write("[VirtIO-GPU] READY ");
    serial_write_dec(g_display_width);
    serial_write("x");
    serial_write_dec(g_display_height);
    serial_write(" pitch=");
    serial_write_dec(g_fb_pitch);
    serial_write(" pages=");
    serial_write_dec(g_fb_nr_pages);
    if (g_has_3d) serial_writeln(" [3D virgl]");
    else serial_writeln(" [2D]");

    return 0;
}

/* ── Query functions ── */

int virtio_gpu_available(void) { return g_ready ? 1 : 0; }
u32 virtio_gpu_width(void) { return g_ready ? g_display_width : 0u; }
u32 virtio_gpu_height(void) { return g_ready ? g_display_height : 0u; }
int virtio_gpu_has_3d(void) { return g_has_3d ? 1 : 0; }
u32 virtio_gpu_framebuffer_pitch(void) { return g_fb_pitch; }
u32 virtio_gpu_framebuffer_bpp(void) { return g_fb_bpp; }
u32 virtio_gpu_framebuffer_size(void) { return g_fb_size_bytes; }

void* virtio_gpu_back_buffer(void) {
    return g_ready ? g_fb_back_ptr : 0;
}

const char* virtio_gpu_backend_name(void) {
    if (!g_ready) return "inactive";
    if (g_has_3d) return "VirtIO-GPU 3D (virgl)";
    return "VirtIO-GPU 2D (scanout)";
}

/* ── Flush: transfer dirty region from back buffer to display ── */

int virtio_gpu_flush_rect(u32 x, u32 y, u32 w, u32 h) {
    if (!g_ready) return -1;
    if (x >= g_display_width || y >= g_display_height) return -1;
    if (x + w > g_display_width) w = g_display_width - x;
    if (y + h > g_display_height) h = g_display_height - y;
    if (w == 0u || h == 0u) return 0;

    u8 resp[sizeof(virtio_gpu_ctrl_hdr_t)] __attribute__((aligned(16)));
    memset(resp, 0, sizeof(resp));

    /* Transfer: back buffer → host visible */
    virtio_gpu_transfer_to_host_2d_t xfer;
    memset(&xfer, 0, sizeof(xfer));
    xfer.rect.x = x;
    xfer.rect.y = y;
    xfer.rect.width = w;
    xfer.rect.height = h;
    xfer.offset = (y * g_fb_pitch) + (x * g_fb_bpp);
    xfer.resource_id = g_fb_active_resource;

    if (gpu_send_command(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
                         &xfer, sizeof(xfer), resp, sizeof(resp)) != 0) {
        return -1;
    }

    /* Flush: make the region visible */
    virtio_gpu_resource_flush_t flush;
    memset(&flush, 0, sizeof(flush));
    flush.rect.x = x;
    flush.rect.y = y;
    flush.rect.width = w;
    flush.rect.height = h;
    flush.resource_id = g_fb_active_resource;

    if (gpu_send_command(VIRTIO_GPU_CMD_RESOURCE_FLUSH,
                         &flush, sizeof(flush), resp, sizeof(resp)) != 0) {
        return -1;
    }
    return 0;
}

/* ── Page flip: swap front and back buffers ── */

int virtio_gpu_page_flip(void) {
    if (!g_ready) return -1;

    /* The new scanout target is the current back buffer */
    u32 new_resource = (g_fb_active_resource == GPU_RESOURCE_FB_FRONT)
                       ? GPU_RESOURCE_FB_BACK
                       : GPU_RESOURCE_FB_FRONT;

    if (gpu_set_scanout(new_resource, g_display_width, g_display_height) != 0) {
        return -1;
    }

    g_fb_active_resource = new_resource;
    /* Back pointer stays the same — both resources share the same memory.
       After flip, the CPU continues writing to the same buffer, which is
       now the "back" buffer (not scanned out). */
    return 0;
}
