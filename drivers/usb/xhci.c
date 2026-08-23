#include "xhci.h"
#include "pci.h"
#include "paging.h"
#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "frame_allocator.h"
#include "usb_msc.h"

/* ═══════════════════════════════════════════════════════════════
 *  xHCI v1 — HID keyboard & mouse via polling
 *
 *  DMA memory: frame_alloc_order() → physically contiguous pages,
 *  identity-mapped (virtual == physical).
 * ═══════════════════════════════════════════════════════════════ */

/* ─── MMIO helpers ─── */
#define REG(base,off)       ((u32)(base) + (u32)(off))
#define xcap_r(reg)         mmio_read32(REG(g_x.mmio,  reg))
#define xop_r(reg)          mmio_read32(REG(g_x.mmio + g_x.op_off, reg))
#define xop_w(reg,v)        mmio_write32(REG(g_x.mmio + g_x.op_off, reg), (v))
#define xdb_w(slot,trgt)    mmio_write32(REG(g_x.mmio + g_x.db_off, (slot)*4u), (trgt))
#define xir_r(i,reg)        mmio_read32(REG(g_x.mmio + g_x.rt_off + (i)*0x20u, reg))
#define xir_w(i,reg,v)      mmio_write32(REG(g_x.mmio + g_x.rt_off + (i)*0x20u, reg), (v))
#define xport_r(p,reg)      xop_r(0x400u + (p)*0x10u + (reg))
#define xport_w(p,reg,v)    xop_w(0x400u + (p)*0x10u + (reg), (v))

/* ─── Constants ─── */
#define CTX32_SZ    32u
#define CTX64_SZ    64u
#define CTX_OFF(n)  ((n)*4u)

/* ─── Global state ─── */
typedef struct {
    /* Controller */
    u32  mmio;
    u32  op_off, db_off, rt_off;
    u8   ready, csz, max_slots, max_ports;
    u32  page_size;

    /* Command ring */
    xhci_trb_t* cmd_trbs;
    u32         cmd_pa;
    u16         cmd_enq;
    u8          cmd_cycle;

    /* Event ring (interrupter 0) */
    xhci_trb_t* evt_trbs;
    u32         evt_pa;
    u16         evt_deq;
    u8          evt_cycle;

    /* Device Context Base Address Array + device context memory */
    u32*        dcbaa;
    u32         dcbaa_pa;
    u8*         dev_ctx;
    u32         dev_ctx_pa;

    /* Per-slot transfer rings (1 page each for EP0 + 1 page for EP1 IN) */
    xhci_trb_t* ep0_ring[XHCI_MAX_SLOTS];
    u32         ep0_ring_pa[XHCI_MAX_SLOTS];
    u16         ep0_enq[XHCI_MAX_SLOTS];
    u8          ep0_cycle[XHCI_MAX_SLOTS];

    xhci_trb_t* ep1_ring[XHCI_MAX_SLOTS];  /* interrupt IN endpoint */
    u32         ep1_ring_pa[XHCI_MAX_SLOTS];
    u16         ep1_enq[XHCI_MAX_SLOTS];
    u8          ep1_cycle[XHCI_MAX_SLOTS];

    /* Per-slot state */
    struct {
        u8   active, port, speed, addr;
        u16  max_pkt;
        u8   config_val;
        u8   intf_proto;   /* 0=none, 1=kb, 2=mouse */
        u8   intf_ep;      /* endpoint number (without direction) */
        u16  intf_mps;
        u8   intf_intv;
        u8   sub_active;   /* interrupt transfer in flight */
        u8   toggle;
        u8   report[8];
        u8   prev_report[8];
    } slots[XHCI_MAX_SLOTS];

    /* HID flags */
    u8   kb_slot, mouse_slot, kb_ready, mouse_ready;
    i32  mouse_dx, mouse_dy;
    u8   mouse_btns;

    /* Stats */
    u32  evts, cmd_fails;

    /* Scratch data for control transfer */
    u8   ctrl_data[XHCI_TRB_BUF_SIZE] __attribute__((aligned(16)));
} xhci_t;

static xhci_t g_x;

/* ─── Forward ─── */
static void usb_delay(u32 ms);
static void* dma_page(void);
static int  xhci_reset(void);
static int  xhci_process_evt(void);
static int  xhci_cmd_enable_slot(u8* out);
static int  xhci_cmd_address_dev(u8 slot, u32 input_ctx_pa);
static int  xhci_cmd_configure_ep(u8 slot, u32 input_ctx_pa);
static int  xhci_enumerate(u8 port);

/* ═══════════════════════════════════════════════════════════════
 *  Utility
 * ═══════════════════════════════════════════════════════════════ */

static void usb_delay(u32 ms) {
    u32 f = timer_frequency();
    u32 need = (ms * f + 999u) / 1000u;
    u32 start = timer_ticks();
    u32 guard = 30000000u;
    if (need == 0u) need = 1u;
    while ((u32)(timer_ticks() - start) < need && guard--) { }
}

static void* dma_page(void) {
    return frame_alloc_order(0u); /* 1 page = 4KB, physically contiguous */
}

static void dma_free(void* p) {
    if (p) frame_free(p);
}

static inline u32 v2p(const void* p) {
    return (u32)p;
}

static void psleep(u32 spins) {
    while (spins--) { __asm__ __volatile__("pause"); }
}

/* Write a TRB to a ring and advance enqueue */
static void ring_put(xhci_trb_t* ring, u16 sz, u16* enq, u8* cycle,
                     u32 p0, u32 p1, u32 p2, u32 p3) {
    u16 idx = *enq;
    ring[idx].params[0] = p0;
    ring[idx].params[1] = p1;
    ring[idx].params[2] = p2;
    ring[idx].params[3] = p3 | (u32)(*cycle);
    idx = (u16)((idx + 1u) % sz);
    if (idx == 0u) {
        /* Link TRB at the end wraps back */
        u16 last = sz - 1u;
        ring[last].params[0] = v2p(ring);
        ring[last].params[1] = 0u;
        ring[last].params[2] = 0u;
        ring[last].params[3] = TRB_TYPE(TRB_LINK) | TRB_TC | (u32)(*cycle);
        *cycle ^= 1u;
    }
    *enq = idx;
}

/* Poll event ring for a specific type; returns 0 on match.
   Does NOT handle other event types (they remain unconsumed on the ring). */
static int poll_cmd_complete(u8* out_slot, u8* out_cc, u32 timeout_ms) {
    u32 deadline = timer_ticks() + (timer_frequency() * timeout_ms + 999u) / 1000u;
    u32 guard = 50000000u;
    while (guard--) {
        u16 deq = g_x.evt_deq;
        xhci_trb_t* trb = &g_x.evt_trbs[deq];
        u32 ctrl = trb->params[3];
        u8  cycle = (u8)(ctrl & 1u);
        if (cycle != g_x.evt_cycle) {
            if ((u32)(timer_ticks() - deadline) < 0x80000000u) return -1;
            psleep(10);
            continue;
        }
        u8 type = (u8)((ctrl >> 10u) & 0x3Fu);
        if (type == TRB_EVT_CMD_COMPLETE) {
            u8 cc = (u8)((trb->params[2] >> 24u) & 0xFFu);
            u8 slot = (u8)((trb->params[2] >> 16u) & 0xFFu);
            if (out_slot) *out_slot = slot;
            if (out_cc)   *out_cc = cc;
            g_x.evt_deq = (u16)((deq + 1u) % XHCI_EVT_RING_SZ);
            if (g_x.evt_deq == 0u) g_x.evt_cycle ^= 1u;
            xir_w(0, XHCI_IR_ERDP, g_x.evt_pa + (u32)g_x.evt_deq * XHCI_TRB_SIZE);
            return (cc == 0u) ? 0 : -2;
        }
        /* Skip non-command-completion events by not consuming them.
           For simplicity we still advance past them. */
        g_x.evt_deq = (u16)((deq + 1u) % XHCI_EVT_RING_SZ);
        if (g_x.evt_deq == 0u) g_x.evt_cycle ^= 1u;
        xir_w(0, XHCI_IR_ERDP, g_x.evt_pa + (u32)g_x.evt_deq * XHCI_TRB_SIZE);
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════════
 *  Reset & init
 * ═══════════════════════════════════════════════════════════════ */

static int xhci_reset(void) {
    u32 timeout;
    /* Wait for CNR */
    timeout = timer_ticks() + (timer_frequency() * 2u + 999u) / 1000u;
    while ((xop_r(XHCI_OP_USBSTS) & XHCI_STS_CNR) &&
           (u32)(timer_ticks() - timeout) < 0x80000000u) { }
    /* Reset */
    xop_w(XHCI_OP_USBCMD, XHCI_CMD_RESET);
    timeout = timer_ticks() + (timer_frequency() * 5u + 999u) / 1000u;
    while ((u32)(timer_ticks() - timeout) < 0x80000000u) {
        if (xop_r(XHCI_OP_USBSTS) & XHCI_STS_HCH) break;
    }
    if (!(xop_r(XHCI_OP_USBSTS) & XHCI_STS_HCH)) return -1;
    return 0;
}

int xhci_init(void) {
    pci_device_t dev;
    u32 hcs1, hcc1, cap_len;
    u32 dev_ctx_bytes;
    u32 sz;

    memset(&g_x, 0, sizeof(g_x));

    /* Find xHCI on PCI */
    if (pci_find_class(0x0Cu, 0x03u, 0x30u, &dev) != 0) {
        serial_writeln("[xHCI] controller not found");
        return -1;
    }

    serial_write("[xHCI] found 0x");
    serial_write_hex(dev.vendor);
    serial_write("/0x");
    serial_write_hex(dev.device);
    serial_putc('\n');

    if (dev.bar0 & 1u) { serial_writeln("[xHCI] BAR0 is IO"); return -1; }
    g_x.mmio = dev.bar0 & 0xFFFFF000u;
    if (!g_x.mmio) return -1;
    if (pci_enable_bus_master(&dev) != 0) return -1;

    /* Map MMIO */
    if (!paging_is_mapped(g_x.mmio)) {
        if (paging_map_range(g_x.mmio, g_x.mmio, 16384u,
                             PAGE_FLAG_RW | PAGE_FLAG_PWT | PAGE_FLAG_PCD) != 0)
            return -1;
    }

    /* Read caps */
    cap_len = xcap_r(XHCI_CAP_CAPLENGTH) & 0xFFu;
    g_x.op_off = cap_len;
    g_x.db_off = xcap_r(XHCI_CAP_DBOFF) & 0xFFFFF0u;
    g_x.rt_off = xcap_r(XHCI_CAP_RTSOFF) & 0xFFFFF0u;

    hcs1 = xcap_r(XHCI_CAP_HCSPARAMS1);
    g_x.max_slots = (u8)(hcs1 & 0xFFu);
    g_x.max_ports = (u8)((hcs1 >> 24) & 0xFFu);
    if (g_x.max_slots > XHCI_MAX_SLOTS) g_x.max_slots = XHCI_MAX_SLOTS;
    if (g_x.max_ports > XHCI_MAX_PORTS) g_x.max_ports = XHCI_MAX_PORTS;

    hcc1 = xcap_r(XHCI_CAP_HCCPARAMS1);
    g_x.csz = (hcc1 & XHCI_HCC1_CSZ) ? 1u : 0u;

    {
        u32 pg = xop_r(XHCI_OP_PAGESIZE);
        g_x.page_size = pg & 0xFFFFu;
        if (g_x.page_size == 0u) g_x.page_size = 1u;
    }

    serial_write("[xHCI] slots=");
    serial_write_dec(g_x.max_slots);
    serial_write(" ports=");
    serial_write_dec(g_x.max_ports);
    serial_write(" csz=");
    serial_write_dec(g_x.csz);
    serial_putc('\n');

    /* Reset */
    if (xhci_reset() != 0) { serial_writeln("[xHCI] reset failed"); return -1; }

    /* ─── Command ring ─── */
    sz = XHCI_CMD_RING_SZ * XHCI_TRB_SIZE;
    sz = (sz + 63u) & ~63u;
    g_x.cmd_trbs = (xhci_trb_t*)dma_page();
    if (!g_x.cmd_trbs) return -1;
    memset(g_x.cmd_trbs, 0, 4096u);
    g_x.cmd_pa = v2p(g_x.cmd_trbs);
    g_x.cmd_enq = 0u;
    g_x.cmd_cycle = 1u;
    /* Link TRB at end */
    {
        u16 last = XHCI_CMD_RING_SZ - 1u;
        g_x.cmd_trbs[last].params[0] = g_x.cmd_pa;
        g_x.cmd_trbs[last].params[3] = TRB_TYPE(TRB_LINK) | TRB_TC | 1u;
    }
    xop_w(XHCI_OP_CRCR, g_x.cmd_pa | XHCI_CRCR_RCS);

    /* ─── Event ring ─── */
    sz = XHCI_EVT_RING_SZ * XHCI_TRB_SIZE;
    sz = (sz + 63u) & ~63u;
    g_x.evt_trbs = (xhci_trb_t*)dma_page();
    if (!g_x.evt_trbs) return -1;
    memset(g_x.evt_trbs, 0, 4096u);
    g_x.evt_pa = v2p(g_x.evt_trbs);
    g_x.evt_deq = 0u;
    g_x.evt_cycle = 1u;

    /* ERST */
    {
        xhci_erst_t* erst = (xhci_erst_t*)dma_page();
        if (!erst) return -1;
        memset(erst, 0, 4096u);
        erst->base_low = g_x.evt_pa;
        erst->size = XHCI_EVT_RING_SZ;
        xir_w(0, XHCI_IR_ERSTSZ, 1u);
        xir_w(0, XHCI_IR_ERSTBA, v2p(erst));
        xir_w(0, XHCI_IR_ERDP, g_x.evt_pa);
    }

    /* ─── DCBAA + device context memory ─── */
    sz = (u32)g_x.max_slots * sizeof(u32);
    sz = (sz + 63u) & ~63u;
    g_x.dcbaa = (u32*)dma_page();
    if (!g_x.dcbaa) return -1;
    memset(g_x.dcbaa, 0, 4096u);
    g_x.dcbaa_pa = v2p(g_x.dcbaa);
    xop_w(XHCI_OP_DCBAAP, g_x.dcbaa_pa);

    /* Device context memory: slot context + 31 EP contexts per slot */
    {
        u32 slot_sz = g_x.csz ? CTX64_SZ : CTX32_SZ;
        u32 ep_sz   = g_x.csz ? CTX64_SZ : CTX32_SZ;
        dev_ctx_bytes = (u32)g_x.max_slots * (slot_sz + 31u * ep_sz);
        dev_ctx_bytes = (dev_ctx_bytes + 4095u) & ~4095u;
        /* Allocate pages: use order for > 4K */
        u8 order = 0u;
        u32 sz_pages = dev_ctx_bytes;
        while (sz_pages > 4096u) { order++; sz_pages -= 4096u; }
        g_x.dev_ctx = (u8*)frame_alloc_order(order);
        if (!g_x.dev_ctx) return -1;
        memset(g_x.dev_ctx, 0, dev_ctx_bytes);
        g_x.dev_ctx_pa = v2p(g_x.dev_ctx);

        /* Fill DCBAA */
        for (u8 i = 0u; i < g_x.max_slots; i++) {
            u32 off = (u32)i * (slot_sz + 31u * ep_sz);
            if (off + slot_sz <= dev_ctx_bytes) {
                g_x.dcbaa[i] = g_x.dev_ctx_pa + off;
            }
        }
    }

    /* Max slots config */
    xop_w(XHCI_OP_CONFIG, (u32)g_x.max_slots);

    /* ─── Allocate per-slot EP0 transfer rings ─── */
    for (u8 i = 0u; i < g_x.max_slots; i++) {
        g_x.ep0_ring[i] = (xhci_trb_t*)dma_page();
        if (!g_x.ep0_ring[i]) continue;
        memset(g_x.ep0_ring[i], 0, 4096u);
        g_x.ep0_ring_pa[i] = v2p(g_x.ep0_ring[i]);
        g_x.ep0_enq[i] = 0u;
        g_x.ep0_cycle[i] = 1u;
        /* Link TRB */
        u16 last = XHCI_TRANSFER_RING_SZ - 1u;
        g_x.ep0_ring[i][last].params[0] = g_x.ep0_ring_pa[i];
        g_x.ep0_ring[i][last].params[3] = TRB_TYPE(TRB_LINK) | TRB_TC | 1u;

        g_x.ep1_ring[i] = (xhci_trb_t*)dma_page();
        if (!g_x.ep1_ring[i]) continue;
        memset(g_x.ep1_ring[i], 0, 4096u);
        g_x.ep1_ring_pa[i] = v2p(g_x.ep1_ring[i]);
        g_x.ep1_enq[i] = 0u;
        g_x.ep1_cycle[i] = 1u;
        g_x.ep1_ring[i][last].params[0] = g_x.ep1_ring_pa[i];
        g_x.ep1_ring[i][last].params[3] = TRB_TYPE(TRB_LINK) | TRB_TC | 1u;
    }

    /* ─── Scratchpad buffers (for real xHC silicon) ─── */
    {
        u32 hcs2 = xcap_r(XHCI_CAP_HCSPARAMS2);
        u32 spb = (hcs2 >> 21u) & 0x1Fu;  /* MaxScratchpadBufs */
        if (spb > 0u) {
            /* Allocate scratchpad pages and put their addresses after
               the slot entries in the DCBAA (offset = max_slots * 4).
               The lowest DCBAA entry for scratchpad arrays holds the
               address of a page containing the scratchpad pointers. */
            u32* sp_array = (u32*)dma_page();
            if (sp_array) {
                for (u32 spi = 0u; spi < spb; spi++) {
                    u32* sp_page = (u32*)dma_page();
                    if (sp_page) {
                        memset(sp_page, 0, 4096u);
                        sp_array[spi] = v2p(sp_page);
                    }
                }
                g_x.dcbaa[(u32)g_x.max_slots] = v2p(sp_array);
            }
        }
    }

    /* ─── Start controller ─── */
    xop_w(XHCI_OP_USBCMD, XHCI_CMD_RUN | XHCI_CMD_INTE | XHCI_CMD_HSEE);
    xir_w(0, XHCI_IR_IMAN, XHCI_IMAN_IP | XHCI_IMAN_IE);

    /* Wait for HCH=0 */
    {
        u32 timeout = timer_ticks() + (timer_frequency() * 2u + 999u) / 1000u;
        while ((xop_r(XHCI_OP_USBSTS) & XHCI_STS_HCH) &&
               (u32)(timer_ticks() - timeout) < 0x80000000u) { }
    }
    {
        u32 timeout = timer_ticks() + (timer_frequency() * 2u + 999u) / 1000u;
        while ((xop_r(XHCI_OP_USBSTS) & XHCI_STS_CNR) &&
               (u32)(timer_ticks() - timeout) < 0x80000000u) { }
    }

    g_x.ready = 1u;
    serial_write("[xHCI] ready at 0x");
    serial_write_hex(g_x.mmio);
    serial_write(", ports=");
    serial_write_dec(g_x.max_ports);
    serial_putc('\n');
    return 0;
}

int xhci_available(void) {
    return g_x.ready ? 1 : 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Commands
 * ═══════════════════════════════════════════════════════════════ */

static int xhci_cmd_enable_slot(u8* out_slot) {
    u16 old_enq = g_x.cmd_enq;
    u8  old_cyc = g_x.cmd_cycle;
    u8  slot = 0u, cc = 0xFFu;

    ring_put(g_x.cmd_trbs, XHCI_CMD_RING_SZ, &g_x.cmd_enq, &g_x.cmd_cycle,
             0, 0, 0, TRB_TYPE(TRB_CMD_ENABLE_SLOT));
    xdb_w(XHCI_DB_HOST, XHCI_DB_CMD);

    if (poll_cmd_complete(&slot, &cc, 5000u) != 0 || cc != 0u || slot == 0u) {
        g_x.cmd_enq = old_enq;
        g_x.cmd_cycle = old_cyc;
        return -1;
    }
    /* slot comes from an event TRB written by the host controller (DMA), so it
     * is untrusted input in the 0..255 range. Every per-slot array is only
     * XHCI_MAX_SLOTS entries, so reject anything that would index past them. */
    if (slot >= XHCI_MAX_SLOTS || slot >= g_x.max_slots) {
        serial_write("[xHCI] rejecting out-of-range slot id ");
        serial_write_dec(slot);
        serial_putc('\n');
        g_x.cmd_enq = old_enq;
        g_x.cmd_cycle = old_cyc;
        return -1;
    }
    if (out_slot) *out_slot = slot;
    g_x.cmd_fails = 0;
    return 0;
}

static int xhci_cmd_address_dev(u8 slot, u32 input_ctx_pa) {
    u16 old_enq = g_x.cmd_enq;
    u8  old_cyc = g_x.cmd_cycle;
    u8  cc = 0xFFu;
    u32 flags = TRB_TYPE(TRB_CMD_ADDRESS_DEV) | ((u32)slot << 24u);

    ring_put(g_x.cmd_trbs, XHCI_CMD_RING_SZ, &g_x.cmd_enq, &g_x.cmd_cycle,
             input_ctx_pa, 0, 0, flags);
    xdb_w(XHCI_DB_HOST, XHCI_DB_CMD);

    if (poll_cmd_complete(0, &cc, 5000u) != 0 || cc != 0u) {
        g_x.cmd_enq = old_enq;
        g_x.cmd_cycle = old_cyc;
        return -1;
    }
    return 0;
}

static int xhci_cmd_configure_ep(u8 slot, u32 input_ctx_pa) {
    u16 old_enq = g_x.cmd_enq;
    u8  old_cyc = g_x.cmd_cycle;
    u8  cc = 0xFFu;
    u32 flags = TRB_TYPE(TRB_CMD_CONFIGURE_EP) | ((u32)slot << 24u);

    ring_put(g_x.cmd_trbs, XHCI_CMD_RING_SZ, &g_x.cmd_enq, &g_x.cmd_cycle,
             input_ctx_pa, 0, 0, flags);
    xdb_w(XHCI_DB_HOST, XHCI_DB_CMD);

    if (poll_cmd_complete(0, &cc, 5000u) != 0 || cc != 0u) {
        g_x.cmd_enq = old_enq;
        g_x.cmd_cycle = old_cyc;
        return -1;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Control transfer (Setup + Data + Status on EP0)
 * ═══════════════════════════════════════════════════════════════ */

static int xhci_ctrl_xfer(u8 slot, u8 bmReqs, u8 bReq, u16 wVal, u16 wIdx,
                            u16 wLen, u8* data, u8 dir_in) {
    u32 ep0_pa;
    u16 enq;
    u8  cycle;
    u8  rc = -1;
    u32 timeout, guard;

    /* Validate the slot index before it is used to index any per-slot array. */
    if (slot >= XHCI_MAX_SLOTS || slot >= g_x.max_slots || !g_x.slots[slot].active) return -1;

    ep0_pa = g_x.ep0_ring_pa[slot];
    enq = g_x.ep0_enq[slot];
    cycle = g_x.ep0_cycle[slot];

    if (!ep0_pa) return -1;

    /* Build Setup TRB.
       The setup packet is encoded in params[0..1]:
       params[0] = (bRequest << 8) | bmReqType   (5 bytes of setup + direction)
       Actually the xHCI setup TRB is:
       params[0] = (wValue << 16) | wLength  ← NO
       Let me use the standard USB setup packet format:
       Setup packet: 8 bytes: bmReqType, bReq, wVal, wIdx, wLen
       In xHCI Setup Stage TRB:
       params[0] = (wValue << 0) | (bmReqType << 24) | (bRequest << 16) — NO
    */
    /* USB setup packet encoded in TRB:
       TRB[0] = (wValue) | (wIndex << 16)  —  bits [31:0]
       TRB[1] = (wLength) | (bmReqType << 24) | (bRequest << 16)  — bits [63:32]
       Actually in xHCI spec (section 6.4.2.1.1):
       Setup TRB dw0[7:0]   = bmReqType
       Setup TRB dw0[15:8]  = bRequest
       Setup TRB dw0[31:16] = wValue
       Setup TRB dw1[15:0]  = wIndex
       Setup TRB dw1[31:16] = wLength
    */
    {
        u32 dw0 = (u32)bmReqs | ((u32)bReq << 8u) | ((u32)wVal << 16u);
        u32 dw1 = (u32)wIdx | ((u32)wLen << 16u);
        u32 trb_type = TRB_SETUP_STAGE;
        u32 trb_flags = TRB_IOC;
        if (dir_in && wLen > 0u) trb_flags |= TRB_DIR_IN;

        /* Write Setup TRB */
        u16 idx = enq;
        g_x.ep0_ring[slot][idx].params[0] = dw0;
        g_x.ep0_ring[slot][idx].params[1] = dw1;
        g_x.ep0_ring[slot][idx].params[2] = 0;
        g_x.ep0_ring[slot][idx].params[3] = TRB_TYPE(trb_type) | trb_flags | (u32)cycle;
        enq = (u16)((idx + 1u) % XHCI_TRANSFER_RING_SZ);
        if (enq == 0u) cycle ^= 1u;
    }

    /* Data Stage (optional) */
    if (wLen > 0u && data) {
        u32 trb_type = TRB_DATA_STAGE;
        u32 dir_flag = dir_in ? TRB_DIR_IN : 0u;
        u16 idx = enq;
        g_x.ep0_ring[slot][idx].params[0] = v2p(data);
        g_x.ep0_ring[slot][idx].params[1] = 0;
        g_x.ep0_ring[slot][idx].params[2] = (u32)wLen;
        g_x.ep0_ring[slot][idx].params[3] = TRB_TYPE(trb_type) | dir_flag | TRB_IOC | TRB_CHAIN | (u32)cycle;
        enq = (u16)((idx + 1u) % XHCI_TRANSFER_RING_SZ);
        if (enq == 0u) cycle ^= 1u;
    }

    /* Status Stage (always) */
    {
        u32 dir_flag = (wLen > 0u && dir_in) ? 0u : TRB_DIR_IN; /* opposite direction */
        u16 idx = enq;
        g_x.ep0_ring[slot][idx].params[0] = 0;
        g_x.ep0_ring[slot][idx].params[1] = 0;
        g_x.ep0_ring[slot][idx].params[2] = 0;
        g_x.ep0_ring[slot][idx].params[3] = TRB_TYPE(TRB_STATUS_STAGE) | dir_flag | TRB_IOC | (u32)cycle;
        enq = (u16)((idx + 1u) % XHCI_TRANSFER_RING_SZ);
        if (enq == 0u) cycle ^= 1u;
    }

    /* Update enqueue and cycle */
    g_x.ep0_enq[slot] = enq;
    g_x.ep0_cycle[slot] = cycle;

    /* Update EP0 endpoint context's TR dequeue pointer.
       For a new slot, the EP0 context already points to the start of the ring.
       We just ring the doorbell for EP0. */
    xdb_w((u32)slot, 1u); /* doorbell for EP0 OUT (=1) */

    /* Poll for transfer event completion */
    timeout = timer_ticks() + (timer_frequency() * 5u + 999u) / 1000u;
    guard = 50000000u;
    while (guard--) {
        u16 deq = g_x.evt_deq;
        xhci_trb_t* trb = &g_x.evt_trbs[deq];
        u32 ctrl = trb->params[3];
        u8  cycle = (u8)(ctrl & 1u);
        if (cycle != g_x.evt_cycle) {
            if ((u32)(timer_ticks() - timeout) < 0x80000000u) break;
            psleep(10);
            continue;
        }
        u8 type = (u8)((ctrl >> 10u) & 0x3Fu);
        if (type == TRB_EVT_TRANSFER) {
            u8 cc = (u8)((trb->params[2] >> 4u) & 0xFFu);
            u8 trb_slot = (u8)((trb->params[2] >> 16u) & 0xFFu);
            u8 trb_ep = (u8)((trb->params[2] >> 24u) & 0xFFu);
            g_x.evt_deq = (u16)((deq + 1u) % XHCI_EVT_RING_SZ);
            if (g_x.evt_deq == 0u) g_x.evt_cycle ^= 1u;
            xir_w(0, XHCI_IR_ERDP, g_x.evt_pa + (u32)g_x.evt_deq * XHCI_TRB_SIZE);
            if (trb_slot == slot && trb_ep == 1u && cc == 0u) {
                rc = 0;
                break;
            }
            /* For control transfers, the status stage completion comes on EP0.
               Accept any completion for our slot on EP0. */
            if (trb_slot == slot && (trb_ep == 1u || trb_ep == 0u)) {
                rc = 0;
                break;
            }
            /* Not ours, continue */
        } else {
            /* Skip other events */
            g_x.evt_deq = (u16)((deq + 1u) % XHCI_EVT_RING_SZ);
            if (g_x.evt_deq == 0u) g_x.evt_cycle ^= 1u;
            xir_w(0, XHCI_IR_ERDP, g_x.evt_pa + (u32)g_x.evt_deq * XHCI_TRB_SIZE);
        }
    }

    return rc;
}

/* ═══════════════════════════════════════════════════════════════
 *  Interrupt transfer submission (EP1 IN)
 * ═══════════════════════════════════════════════════════════════ */

static int xhci_submit_intr(u8 slot) {
    u16 enq;
    u8  cycle;
    u16 idx;
    u32 trb_pa;

    if (slot >= g_x.max_slots || !g_x.slots[slot].active) return -1;

    enq = g_x.ep1_enq[slot];
    cycle = g_x.ep1_cycle[slot];
    idx = enq;

    /* Write Normal TRB pointing to the slot's report buffer.
       Single-TRB TD: no CHAIN bit. IOC fires an event on completion. */
    trb_pa = v2p(g_x.slots[slot].report);
    g_x.ep1_ring[slot][idx].params[0] = trb_pa;
    g_x.ep1_ring[slot][idx].params[1] = 0;
    g_x.ep1_ring[slot][idx].params[2] = (u32)g_x.slots[slot].intf_mps;
    g_x.ep1_ring[slot][idx].params[3] = TRB_TYPE(TRB_NORMAL) | TRB_IOC | (u32)cycle;

    enq = (u16)((idx + 1u) % XHCI_TRANSFER_RING_SZ);
    /* When enq wraps to 0, the link TRB at the last slot (index 15) will
       be followed by the controller, which toggles the cycle bit and
       restarts at index 0.  We allow TRBs at index 0 with the new cycle. */
    if (enq == 0u) cycle ^= 1u;
    g_x.ep1_enq[slot] = enq;
    g_x.ep1_cycle[slot] = cycle;

    /* Ring doorbell for the interrupt IN endpoint using DCI.
       DCI = endpoint_num * 2 + (dir == IN ? 1 : 0)
       For EP1 IN: DCI = 1*2 + 1 = 3 */
    xdb_w((u32)slot, (u32)((u32)g_x.slots[slot].intf_ep * 2u + 1u));

    g_x.slots[slot].sub_active = 1u;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Event processing
 * ═══════════════════════════════════════════════════════════════ */

static int xhci_process_evt(void) {
    u32 processed = 0u;
    u8  port_change = 0u;

    while (processed < XHCI_EVT_RING_SZ) {
        u16 deq = g_x.evt_deq;
        xhci_trb_t* trb = &g_x.evt_trbs[deq];
        u32 ctrl = trb->params[3];
        u8  cycle = (u8)(ctrl & 1u);
        if (cycle != g_x.evt_cycle) break;

        u8 type = (u8)((ctrl >> 10u) & 0x3Fu);

        switch (type) {
        case TRB_EVT_TRANSFER: {
            u8 slot = (u8)((trb->params[2] >> 16u) & 0xFFu);
            u8 ep   = (u8)((trb->params[2] >> 24u) & 0xFFu);
            u8 cc   = (u8)((trb->params[2] >> 4u) & 0xFFu);

            /* Compute endpoint DCI from USB endpoint number:
               DCI = endpoint_num * 2 + (dir == IN ? 1 : 0)
               For EP1 IN (endpoint=1, dir=IN): DCI = 1*2 + 1 = 3
               slot is device-DMA supplied (0..255): bound it before indexing. */
            if (slot >= XHCI_MAX_SLOTS) break;
            u8 exp_dci = (u8)((u32)g_x.slots[slot].intf_ep * 2u + 1u);
            if (ep == exp_dci && cc == 0u &&
                g_x.slots[slot].active && g_x.slots[slot].intf_ep > 0u) {
                u8* r = g_x.slots[slot].report;
                u8  proto = g_x.slots[slot].intf_proto;

                if (proto == 1u) { /* Keyboard */
                    u8 shift = (r[0] & 0x22u) ? 1u : 0u;
                    for (u32 i = 2u; i < 8u; i++) {
                        u8 usage = r[i];
                        if (usage == 0u) continue;
                        u8 found = 0u;
                        for (u32 j = 2u; j < 8u; j++) {
                            if (g_x.slots[slot].prev_report[j] == usage) { found = 1u; break; }
                        }
                        if (found) continue;
                        char c = 0;
                        static const char digits[] = "1234567890";
                        static const char sdigits[] = "!@#$%^&*()";
                        if (usage >= 0x04u && usage <= 0x1Du) {
                            c = (char)('a' + (usage - 0x04u));
                            if (shift) c = (char)(c - ('a' - 'A'));
                        } else if (usage >= 0x1Eu && usage <= 0x27u)
                            c = shift ? sdigits[usage - 0x1Eu] : digits[usage - 0x1Eu];
                        else if (usage == 0x28u) c = '\n';
                        else if (usage == 0x2Au) c = '\b';
                        else if (usage == 0x2Cu) c = ' ';
                        else if (usage == 0x2Du) c = shift ? '_' : '-';
                        else if (usage == 0x2Eu) c = shift ? '+' : '=';
                        else if (usage == 0x2Fu) c = shift ? '{' : '[';
                        else if (usage == 0x30u) c = shift ? '}' : ']';
                        else if (usage == 0x31u) c = shift ? '|' : '\\';
                        else if (usage == 0x33u) c = shift ? ':' : ';';
                        else if (usage == 0x34u) c = shift ? '\"' : '\'';
                        else if (usage == 0x36u) c = shift ? '<' : ',';
                        else if (usage == 0x37u) c = shift ? '>' : '.';
                        else if (usage == 0x38u) c = shift ? '/' : '?';
                        if (c) keyboard_inject_char(c);
                    }
                    memcpy(g_x.slots[slot].prev_report, r, 8u);
                } else if (proto == 2u) { /* Mouse */
                    i32 dx = (i32)(*(char*)&r[1]);
                    i32 dy = (i32)(*(char*)&r[2]) * (-1);
                    u8 btns = r[0] & 0x07u;
                    g_x.mouse_dx += dx;
                    g_x.mouse_dy += dy;
                    g_x.mouse_btns = btns;
                }
                g_x.slots[slot].sub_active = 0u;
            }
            break;
        }
        case TRB_EVT_PORT_STATUS: {
            u32 port_id = (trb->params[0] >> 24u) & 0xFFu;
            if (port_id > 0u && port_id <= (u32)g_x.max_ports) {
                port_change = (u8)(port_id - 1u);
            }
            break;
        }
        case TRB_EVT_CMD_COMPLETE:
        default:
            break;
        }

        g_x.evt_deq = (u16)((deq + 1u) % XHCI_EVT_RING_SZ);
        if (g_x.evt_deq == 0u) g_x.evt_cycle ^= 1u;
        processed++;
    }

    if (processed > 0u) {
        xir_w(0, XHCI_IR_ERDP, g_x.evt_pa + (u32)g_x.evt_deq * XHCI_TRB_SIZE);
    }
    g_x.evts += processed;
    return port_change ? (int)port_change + 1 : 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Device enumeration (HID keyboard/mouse)
 * ═══════════════════════════════════════════════════════════════ */

static int xhci_get_dev_desc(u8 slot, u8* out18) {
    if (!out18) return -1;
    memset(out18, 0, 18u);
    return xhci_ctrl_xfer(slot,
                USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                (u16)(USB_DESC_DEVICE << 8u), 0u, 18u, out18, 1u);
}

static int xhci_get_config_desc(u8 slot, u8* buf, u16 len) {
    if (!buf || len < 9u) return -1;
    memset(buf, 0, len);
    return xhci_ctrl_xfer(slot,
                USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                (u16)(USB_DESC_CONFIG << 8u), 0u, len, buf, 1u);
}

static int xhci_set_config(u8 slot, u8 config_val) {
    return xhci_ctrl_xfer(slot,
                USB_DIR_OUT, USB_REQ_SET_CONFIG,
                (u16)config_val, 0u, 0u, 0, 0u);
}

static int xhci_set_protocol(u8 slot, u8 intf_num, u8 protocol) {
    (void)protocol;
    return xhci_ctrl_xfer(slot,
                0x21u, USB_REQ_SET_PROTOCOL, 0u, (u16)intf_num, 0u, 0, 0u);
}

static int xhci_set_idle(u8 slot, u8 intf_num) {
    return xhci_ctrl_xfer(slot,
                0x21u, USB_REQ_SET_IDLE, 0u, (u16)intf_num, 0u, 0, 0u);
}

static int xhci_enumerate(u8 port) {
    u8  slot_id = 0u;
    u32 ps;
    u8  speed;
    u8  max_pkt;
    u8  dev_desc[18];
    u16 total_len;
    u8  config_buf[128];
    u8  config_val = 0u;
    u8  intf_num = 0u, protocol = 0u;
    u8  intf_ep = 0u;
    u16 intf_mps = 0u;
    u8  intf_intv = 0u;
    u16 speed_to_mps[5] = {0, 8, 8, 64, 512}; /* LS, FS, HS, SS */

    if (port >= g_x.max_ports) return -1;

    /* Reset port */
    {
        u32 ps2 = xport_r(port, 0);
        if ((ps2 & XHCI_PORTSC_CCS) == 0u) return -2;
        /* Power on */
        if ((ps2 & XHCI_PORTSC_PP) == 0u) {
            xport_w(port, 0, ps2 | XHCI_PORTSC_PP);
            usb_delay(20u);
        }
        /* Reset */
        ps2 = xport_r(port, 0);
        xport_w(port, 0, ps2 | XHCI_PORTSC_PR);
        usb_delay(100u);
        /* Wait for PRC */
        {
            u32 timeout = timer_ticks() + (timer_frequency() * 1u + 999u) / 1000u;
            while ((u32)(timer_ticks() - timeout) < 0x80000000u) {
                ps2 = xport_r(port, 0);
                if ((ps2 & XHCI_PORTSC_PR) == 0u) break;
            }
        }
        usb_delay(10u);
        ps2 = xport_r(port, 0);
        if ((ps2 & XHCI_PORTSC_PED) == 0u) return -3;
        /* Clear change bits */
        xport_w(port, 0, ps2 & ~(XHCI_PORTSC_CSC | XHCI_PORTSC_PRC | XHCI_PORTSC_PLC | XHCI_PORTSC_OCC));
    }

    ps = xport_r(port, 0);
    if ((ps & XHCI_PORTSC_CCS) == 0u) return -4;
    speed = (u8)((ps >> XHCI_PORTSC_SPEED_SHIFT) & 0x0Fu);
    max_pkt = (speed < 5u) ? speed_to_mps[speed] : 8u;
    if (max_pkt == 0u) max_pkt = 8u;

    /* Enable slot */
    if (xhci_cmd_enable_slot(&slot_id) != 0 || slot_id == 0u ||
        slot_id >= XHCI_MAX_SLOTS || slot_id >= g_x.max_slots) return -5;

    serial_write("[xHCI] slot ");
    serial_write_dec(slot_id);
    serial_write(" port ");
    serial_write_dec(port + 1u);
    serial_write(" speed=");
    serial_write_dec(speed);
    serial_putc('\n');

    /* Init slot state */
    memset(&g_x.slots[slot_id], 0, sizeof(g_x.slots[slot_id]));
    g_x.slots[slot_id].active = 1u;
    g_x.slots[slot_id].port = port;
    g_x.slots[slot_id].speed = speed;
    g_x.slots[slot_id].max_pkt = max_pkt;

    /* ─── Build Input Context for Address Device ─── */
    {
        u8* input_ctx = (u8*)dma_page();
        u32 input_pa;
        u32* add_flags;
        u32* slt;

        if (!input_ctx) return -6;
        input_pa = v2p(input_ctx);
        memset(input_ctx, 0, 4096u);

        /* Input context header: drop_flags[0], add_flags[1], rsvd[2..7] */
        add_flags = (u32*)input_ctx;
        add_flags[0] = 0u;  /* drop none */
        add_flags[1] = 3u;  /* add slot (bit0) + EP0 OUT (bit1) */

        /* Slot context starts at offset 32 (after 8 dwords header) */
        slt = (u32*)(input_ctx + 32u);

        /* slot_ctx[0]: context entries=1, port number */
        slt[0] = (1u << 16u) | ((u32)(port + 1u) & 0xFFu);
        /* slot_ctx[1]: max packet, speed */
        slt[1] = ((u32)max_pkt << 20u) | (u32)speed;

        /* EP0 context starts at slot_ctx_size bytes after slot context.
           With 32-byte contexts: slot=32 bytes, ep=32 bytes.
           EP0 offset = 32 (header) + 32 (slot) = 64 */
        {
            u32 ep_sz = g_x.csz ? CTX64_SZ : CTX32_SZ;
            u32 ep0_off = 32u + ep_sz;
            u32* ep0 = (u32*)(input_ctx + ep0_off);

            /* EP0 context:
               dw0 = (max_pkt << 16) | (0 << 3) | 4 (control)
               dw1 = TR dequeue pointer low
               dw2 = (interval << 16) | ... 
            */
            ep0[0] = ((u32)max_pkt << 16u) | 4u; /* EP type = Control (4) */
            ep0[1] = g_x.ep0_ring_pa[slot_id] | 1u; /* DCS = 1 (first cycle) */
            ep0[2] = 0u;
            ep0[3] = 0u;
        }

        /* DCBAA points to device context which will be updated by the cmd.
           We temporarily point it at our input context so the controller
           can write back the slot ID etc. */
        g_x.dcbaa[slot_id] = input_pa;

        /* Address Device */
        if (xhci_cmd_address_dev(slot_id, input_pa) != 0) {
            serial_writeln("[xHCI] address device failed");
            g_x.slots[slot_id].active = 0u;
            g_x.dcbaa[slot_id] = 0u;
            dma_free(input_ctx);
            return -7;
        }

        /* Copy the device context back to our real DCBAA */
        {
            u32 real_ctx_pa = g_x.dcbaa[slot_id];
            if (real_ctx_pa != input_pa && real_ctx_pa != 0u) {
                /* Controller updated DCBAA entry */
            } else {
                /* Use the pre-allocated device context */
                u32 slot_sz = g_x.csz ? CTX64_SZ : CTX32_SZ;
                u32 off = (u32)slot_id * (slot_sz + 31u * (g_x.csz ? CTX64_SZ : CTX32_SZ));
                g_x.dcbaa[slot_id] = g_x.dev_ctx_pa + off;
            }
        }

        dma_free(input_ctx);
    }

    /* Update max packet from device descriptor */
    if (xhci_get_dev_desc(slot_id, dev_desc) == 0) {
        u8 desc_max_pkt = dev_desc[7];
        if (desc_max_pkt > 0u && desc_max_pkt <= 64u) {
            g_x.slots[slot_id].max_pkt = desc_max_pkt;
        }
    }

    serial_write("[xHCI] slot ");
    serial_write_dec(slot_id);
    serial_write(" addressed\n");

    /* Get config descriptor (first 9 bytes for total length) */
    if (xhci_get_config_desc(slot_id, config_buf, 9u) != 0) {
        serial_writeln("[xHCI] get config header failed");
        return -8;
    }
    total_len = (u16)config_buf[2] | ((u16)config_buf[3] << 8u);
    config_val = config_buf[5];
    if (total_len < 9u || total_len > sizeof(config_buf)) {
        total_len = sizeof(config_buf);
    }

    /* Get full config descriptor */
    if (xhci_get_config_desc(slot_id, config_buf, total_len) != 0) {
        serial_writeln("[xHCI] get full config failed");
        return -9;
    }

    /* Parse descriptors for HID interface */        {
            for (u32 at = 0u; at + 2u <= (u32)total_len;) {
            u8 len = config_buf[at];
            u8 type = config_buf[at + 1u];
            if (len < 2u || at + (u32)len > (u32)total_len) break;

            if (type == USB_DESC_INTERFACE && len >= 9u) {
                u8 cls = config_buf[at + 5u];
                u8 sub = config_buf[at + 6u];
                u8 proto = config_buf[at + 7u];
                if (cls == USB_CLASS_HID && sub == USB_HID_SUBCLASS_BOOT &&
                    (proto == USB_HID_PROTOCOL_KEYBOARD || proto == USB_HID_PROTOCOL_MOUSE)) {
                    intf_num = config_buf[at + 2u];
                    protocol = proto;
                    /* HID interface found */
                } else {
                    protocol = 0u;
                }
            } else if (type == USB_DESC_ENDPOINT && len >= 7u && protocol != 0u) {
                if ((config_buf[at + 2u] & 0x80u) && (config_buf[at + 3u] & 0x03u) == 0x03u) {
                    intf_ep = (u8)(config_buf[at + 2u] & 0x0Fu);
                    intf_mps = (u16)config_buf[at + 4u] | ((u16)config_buf[at + 5u] << 8u);
                    intf_intv = config_buf[at + 6u];
                    break;
                }
            }
            at += len;
        }
    }

    if (protocol == 0u || intf_ep == 0u || intf_mps == 0u) {
        serial_writeln("[xHCI] no HID boot device found on this port");
        g_x.slots[slot_id].active = 0u;
        return -10;
    }

    /* Set configuration */
    if (xhci_set_config(slot_id, config_val) != 0) {
        serial_writeln("[xHCI] set config failed");
        return -11;
    }
    usb_delay(5u);

    /* Set protocol (boot) and idle */
    (void)xhci_set_protocol(slot_id, intf_num, protocol);
    (void)xhci_set_idle(slot_id, intf_num);

    /* ─── Configure non-default endpoint (interrupt IN) ─── */
    {
        u8* input_ctx = (u8*)dma_page();
        u32 input_pa;
        u32* add_flags;
        u32* slt;
        u32 ep_sz, ep_offset;

        if (!input_ctx) return -12;
        input_pa = v2p(input_ctx);
        memset(input_ctx, 0, 4096u);

        /* Input context header */
        add_flags = (u32*)input_ctx;
        add_flags[0] = 0u;  /* drop none */
        add_flags[1] = 1u;  /* only slot context (already have EP0 + adding new EP) */

        ep_sz = g_x.csz ? CTX64_SZ : CTX32_SZ;

        /* Slot context at offset 32 */
        slt = (u32*)(input_ctx + 32u);
        slt[0] = (3u << 16u) | ((u32)(port + 1u) & 0xFFu); /* 3 context entries: slot + EP0 + EP1 */
        slt[1] = ((u32)g_x.slots[slot_id].max_pkt << 20u) | (u32)speed;

        /* EP0 context at offset 32 + ep_sz */
        ep_offset = 32u + ep_sz;
        {
            u32* ep0 = (u32*)(input_ctx + ep_offset);
            ep0[0] = ((u32)g_x.slots[slot_id].max_pkt << 16u) | 4u; /* Control */
            ep0[1] = g_x.ep0_ring_pa[slot_id] | 1u;
        }

        /* Interrupt IN EP context at offset 32 + ep_sz * dci
           dci = endpoint * 2 + (dir == IN ? 1 : 0)
           EP0 OUT=1, EP1 OUT=2, EP1 IN=3 */
        {
            u32 dci = (u32)intf_ep * 2u + 1u; /* IN endpoint */
            u32 ep_ctx_off = 32u + ep_sz * dci;
            u32* ep_ctx = (u32*)(input_ctx + ep_ctx_off);

            /* EP type: Interrupt IN = 7 (bits [5:3]) */
            ep_ctx[0] = ((u32)intf_mps << 16u) | (7u << 3u);

            /* TR dequeue pointer: DCS=1, point to transfer ring */
            ep_ctx[1] = g_x.ep1_ring_pa[slot_id] | 1u;

            /* Average TRB length in dw2, interval in dw3 */
            ep_ctx[2] = 0u;
            ep_ctx[3] = 0u;

            /* Set add_flags to include this endpoint */
            add_flags[1] |= (1u << dci); /* add endpoint context */

            /* Also add slot context (bit 0) */
            add_flags[1] |= 1u;
        }

        /* Configure Endpoint */
        if (xhci_cmd_configure_ep(slot_id, input_pa) != 0) {
            serial_writeln("[xHCI] configure EP failed");
            dma_free(input_ctx);
            return -13;
        }

        dma_free(input_ctx);
    }

    /* Save HID info */
    g_x.slots[slot_id].config_val = config_val;
    g_x.slots[slot_id].intf_proto = protocol;
    g_x.slots[slot_id].intf_ep = intf_ep;
    g_x.slots[slot_id].intf_mps = intf_mps;
    g_x.slots[slot_id].intf_intv = intf_intv;
    g_x.slots[slot_id].sub_active = 0u;

    if (protocol == USB_HID_PROTOCOL_KEYBOARD) {
        g_x.kb_slot = slot_id;
        g_x.kb_ready = 1u;
        serial_writeln("[xHCI] USB HID keyboard ready");
    } else if (protocol == USB_HID_PROTOCOL_MOUSE) {
        g_x.mouse_slot = slot_id;
        g_x.mouse_ready = 1u;
        serial_writeln("[xHCI] USB HID mouse ready");
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 *  Poll
 * ═══════════════════════════════════════════════════════════════ */

void xhci_poll(void) {
    u32 sts;
    if (!g_x.ready) return;

    /* Process events */
    (void)xhci_process_evt();

    /* Check USBSTS for port change */
    sts = xop_r(XHCI_OP_USBSTS);
    if (sts & XHCI_STS_PCD) {
        xop_w(XHCI_OP_USBSTS, XHCI_STS_PCD); /* clear */
        /* Scan ports */
        for (u8 p = 0u; p < g_x.max_ports; p++) {
            u32 ps = xport_r(p, 0);
            u8  connected = (ps & XHCI_PORTSC_CCS) ? 1u : 0u;

            if (ps & (XHCI_PORTSC_CSC | XHCI_PORTSC_PRC | XHCI_PORTSC_PLC)) {
                u8 already = 0u;
                serial_write("[xHCI] port ");
                serial_write_dec(p + 1u);
                serial_write(" change: ");
                serial_write_hex(ps);
                serial_putc('\n');

                for (u8 s = 0u; s < g_x.max_slots; s++) {
                    if (g_x.slots[s].active && g_x.slots[s].port == p) {
                        already = 1u;
                        break;
                    }
                }
                if (connected && !already) {
                    int rc = xhci_enumerate(p);
                    if (rc == 0) {
                        serial_write("[xHCI] HID enumerated on port ");
                        serial_write_dec(p + 1u);
                        serial_putc('\n');
                    } else {
                        serial_write("[xHCI] enum port ");
                        serial_write_dec(p + 1u);
                        serial_write(" failed: ");
                        serial_write_dec(rc);
                        serial_putc('\n');
                    }
                }
                /* Clear change bits */
                xport_w(p, 0, ps & ~(XHCI_PORTSC_CSC | XHCI_PORTSC_PRC |
                                      XHCI_PORTSC_PLC | XHCI_PORTSC_OCC));
            }
        }
    }

    /* Resubmit interrupt transfers for HID devices */
    for (u8 s = 1u; s < g_x.max_slots; s++) {
        if (!g_x.slots[s].active || g_x.slots[s].intf_proto == 0u) continue;
        if (g_x.slots[s].sub_active) continue;
        xhci_submit_intr(s);
    }

    /* Mouse delta is NOT zeroed here.  The consumer (e.g. LastGUI port)
       calls xhci_mouse_state() to drain the accumulated delta. */
}

/* ═══════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════ */

int xhci_hid_keyboard_ready(void) {
    return g_x.kb_ready ? 1 : 0;
}

int xhci_hid_mouse_ready(void) {
    return g_x.mouse_ready ? 1 : 0;
}

void xhci_mouse_state(i32* dx, i32* dy, u8* buttons) {
    if (dx)      *dx = g_x.mouse_dx;
    if (dy)      *dy = g_x.mouse_dy;
    if (buttons) *buttons = g_x.mouse_btns;
    g_x.mouse_dx = 0;
    g_x.mouse_dy = 0;
}
