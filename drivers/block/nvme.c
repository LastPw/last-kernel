/*
 * NVMe (Non-Volatile Memory Express) driver for PCIe-based SSDs
 * Spec: NVM Express 1.4
 *
 * Scope: one controller, namespace 1, a single I/O queue pair, 512-byte LBAs,
 * polled completions. That matches what system/disk.c can consume (32-bit LBA,
 * one sector per call) and keeps every DMA buffer a fixed kernel static.
 *
 * Queue memory is addressed by its virtual address because the kernel maps
 * its own statics identity-style; devices/virtio_blk.c makes the same
 * assumption.
 */
#include "pci.h"
#include "io.h"
#include "string.h"
#include "serial.h"
#include "paging.h"
#include "types.h"

/* NVMe PCI class: 01 (mass storage) / 08 (NVM) / 02 (NVMe) */
#define NVME_CLASS_CODE    0x01u
#define NVME_SUBCLASS      0x08u
#define NVME_PROG_IF       0x02u

/* Controller registers, offsets from BAR0 */
#define NVME_REG_CAP       0x00u   /* Controller Capabilities (64-bit) */
#define NVME_REG_VS        0x08u   /* Version */
#define NVME_REG_CC        0x14u   /* Controller Configuration */
#define NVME_REG_CSTS      0x1Cu   /* Controller Status */
#define NVME_REG_AQA       0x24u   /* Admin Queue Attributes */
#define NVME_REG_ASQ       0x28u   /* Admin Submission Queue Base (64-bit) */
#define NVME_REG_ACQ       0x30u   /* Admin Completion Queue Base (64-bit) */
#define NVME_REG_DOORBELL  0x1000u /* Start of the doorbell region */

/* Controller Configuration fields */
#define NVME_CC_EN         (1u << 0)
#define NVME_CC_CSS_NVM    (0u << 4)   /* NVM command set */
#define NVME_CC_MPS_4K     (0u << 7)   /* page size = 2^(12+MPS) = 4096 */
#define NVME_CC_AMS_RR     (0u << 11)  /* round robin arbitration */
#define NVME_CC_SHN_NONE   (0u << 14)
#define NVME_CC_IOSQES     (6u << 16)  /* SQ entry = 2^6 = 64 bytes */
#define NVME_CC_IOCQES     (4u << 20)  /* CQ entry = 2^4 = 16 bytes */

/* Controller Status fields */
#define NVME_CSTS_RDY      (1u << 0)
#define NVME_CSTS_CFS      (1u << 1)   /* controller fatal status */

/* Opcodes */
#define NVME_ADMIN_CREATE_IO_SQ  0x01u
#define NVME_ADMIN_CREATE_IO_CQ  0x05u
#define NVME_ADMIN_IDENTIFY      0x06u
#define NVME_IO_WRITE            0x01u
#define NVME_IO_READ             0x02u

/* Identify CNS values */
#define NVME_CNS_NAMESPACE       0x00u
#define NVME_CNS_CONTROLLER      0x01u

#define NVME_ADMIN_QUEUE_SIZE  8u     /* entries; we only ever have one in flight */
#define NVME_IO_QUEUE_SIZE     8u
#define NVME_IO_QUEUE_ID       1u

/* The only LBA size the disk layer can express. */
#define NVME_LBA_SIZE          512u

/* Submission Queue Entry, 64 bytes. */
typedef struct __attribute__((packed)) {
    u32 cdw0;      /* opcode | fuse | CID<<16 */
    u32 nsid;
    u32 cdw2;
    u32 cdw3;
    u64 mptr;
    u64 prp1;
    u64 prp2;
    u32 cdw10;
    u32 cdw11;
    u32 cdw12;
    u32 cdw13;
    u32 cdw14;
    u32 cdw15;
} nvme_sqe_t;

/* Completion Queue Entry, 16 bytes. */
typedef struct __attribute__((packed)) {
    u32 dw0;
    u32 dw1;
    u16 sq_head;
    u16 sq_id;
    u16 cid;
    u16 status;    /* bit 0 = phase tag, bits 15:1 = status field */
} nvme_cqe_t;

/*
 * Identify Namespace, the first 128 bytes. The LBA Format array starts at
 * offset 128, so this struct must be exactly 128 bytes for lbaf[] indexing
 * off its end to land correctly; _Static_assert below enforces that.
 */
typedef struct __attribute__((packed)) {
    u64 nsze;          /* namespace size in blocks */
    u64 ncap;
    u64 nuse;
    u8  nsfeat;
    u8  nlbaf;         /* number of LBA formats, 0-based */
    u8  flbas;         /* bits 3:0 = index of the format in use */
    u8  mc;
    u8  dpc;
    u8  dps;
    u8  nmic;
    u8  rescap;
    u8  fpi;
    u8  dlfeat;
    u16 nawun;
    u16 nawupf;
    u16 nacwu;
    u16 nabsn;
    u16 nabo;
    u16 nabspf;
    u16 noiob;
    u8  nvmcap[16];
    u8  reserved64[40];
    u8  nguid[16];
    u64 eui64;
} nvme_id_ns_t;

_Static_assert(sizeof(nvme_sqe_t) == 64, "NVMe SQE must be 64 bytes");
_Static_assert(sizeof(nvme_cqe_t) == 16, "NVMe CQE must be 16 bytes");
_Static_assert(sizeof(nvme_id_ns_t) == 128, "Identify Namespace prefix must be 128 bytes");

/* Controller state */
static u32 g_bar0;
static u32 g_doorbell_stride;   /* bytes between adjacent doorbell registers */
static u8  g_ready;
static u32 g_namespace_blocks;
static u16 g_next_cid;

/* Admin queue cursors. phase = the tag value that marks a fresh completion. */
static u16 g_admin_sq_tail;
static u16 g_admin_cq_head;
static u8  g_admin_phase;

/* I/O queue cursors. */
static u16 g_io_sq_tail;
static u16 g_io_cq_head;
static u8  g_io_phase;

/*
 * Queues must be page-aligned and physically contiguous (we set PC=1 when
 * creating the I/O pair, and the admin queues are always contiguous).
 */
static nvme_sqe_t g_admin_sq[NVME_ADMIN_QUEUE_SIZE] __attribute__((aligned(4096)));
static nvme_cqe_t g_admin_cq[NVME_ADMIN_QUEUE_SIZE] __attribute__((aligned(4096)));
static nvme_sqe_t g_io_sq[NVME_IO_QUEUE_SIZE] __attribute__((aligned(4096)));
static nvme_cqe_t g_io_cq[NVME_IO_QUEUE_SIZE] __attribute__((aligned(4096)));

/* DMA buffers. Page-aligned so a single PRP entry never crosses a page. */
static u8 g_identify_buffer[4096] __attribute__((aligned(4096)));
static u8 g_io_buffer[4096] __attribute__((aligned(4096)));

static void nvme_write_reg32(u32 offset, u32 value) {
    mmio_write32(g_bar0 + offset, value);
}

static u32 nvme_read_reg32(u32 offset) {
    return mmio_read32(g_bar0 + offset);
}

static void nvme_write_reg64(u32 offset, u64 value) {
    /* CAP.AQA/ASQ/ACQ are 64-bit; low dword first is required by the spec. */
    mmio_write32(g_bar0 + offset, (u32)value);
    mmio_write32(g_bar0 + offset + 4u, (u32)(value >> 32));
}

/*
 * Doorbell layout: SQ y tail at 0x1000 + (2y)*stride,
 *                  CQ y head at 0x1000 + (2y+1)*stride.
 * The stride comes from CAP.DSTRD and is not always 4 bytes, so it cannot be
 * hardcoded.
 */
static void nvme_ring_sq(u32 queue_id, u16 tail) {
    nvme_write_reg32(NVME_REG_DOORBELL + (2u * queue_id) * g_doorbell_stride, tail);
}

static void nvme_ring_cq(u32 queue_id, u16 head) {
    nvme_write_reg32(NVME_REG_DOORBELL + (2u * queue_id + 1u) * g_doorbell_stride, head);
}

static void nvme_report_status(const char* what, u16 status_phase) {
    /* Status field is bits 15:1; SC in 8:1, SCT in 11:9. */
    serial_write("[LastOS] nvme ");
    serial_write(what);
    serial_write(" failed, status=0x");
    serial_write_hex((u32)(status_phase >> 1));
    serial_putc('\n');
}

static int nvme_wait_ready(u8 target) {
    u32 attempt;
    u32 spin;

    for (attempt = 0u; attempt < 20000u; attempt++) {
        u32 csts = nvme_read_reg32(NVME_REG_CSTS);
        if ((csts & NVME_CSTS_CFS) != 0u) {
            serial_writeln("[LastOS] nvme controller reported a fatal error");
            return -1;
        }
        if (((csts & NVME_CSTS_RDY) != 0u) == (target != 0u)) return 0;
        for (spin = 0u; spin < 200u; spin++) io_wait();
    }
    return -1;
}

/*
 * Submit one command and poll its completion.
 *
 * A completion is fresh when its phase tag equals the phase we expect for this
 * pass over the ring; the expected phase flips every time the head wraps. The
 * CQE is read through a volatile pointer so the poll actually re-reads memory
 * the controller writes behind the compiler's back.
 */
static int nvme_submit_sync(nvme_sqe_t* sqe,
                            nvme_sqe_t* sq, nvme_cqe_t* cq,
                            u16 queue_size, u32 queue_id,
                            u16* sq_tail, u16* cq_head, u8* phase,
                            const char* what) {
    volatile nvme_cqe_t* cqe;
    u16 tail = *sq_tail;
    u16 head = *cq_head;
    u16 status_phase;
    u32 retry;

    memcpy(&sq[tail], sqe, sizeof(nvme_sqe_t));
    tail = (u16)((tail + 1u) % queue_size);
    *sq_tail = tail;
    nvme_ring_sq(queue_id, tail);

    cqe = (volatile nvme_cqe_t*)&cq[head];
    for (retry = 0u; retry < 200000u; retry++) {
        status_phase = cqe->status;
        if ((u8)(status_phase & 1u) == *phase) {
            head = (u16)((head + 1u) % queue_size);
            if (head == 0u) *phase ^= 1u;   /* ring wrapped: tags invert */
            *cq_head = head;
            nvme_ring_cq(queue_id, head);

            if ((status_phase >> 1) != 0u) {
                nvme_report_status(what, status_phase);
                return -1;
            }
            return 0;
        }
        io_wait();
    }

    serial_write("[LastOS] nvme ");
    serial_write(what);
    serial_writeln(" timed out waiting for completion");
    return -1;
}

static int nvme_admin_command(nvme_sqe_t* sqe, const char* what) {
    return nvme_submit_sync(sqe, g_admin_sq, g_admin_cq,
                            NVME_ADMIN_QUEUE_SIZE, 0u,
                            &g_admin_sq_tail, &g_admin_cq_head, &g_admin_phase,
                            what);
}

static void nvme_prepare(nvme_sqe_t* sqe, u8 opcode) {
    memset(sqe, 0, sizeof(*sqe));
    sqe->cdw0 = (u32)opcode | ((u32)(g_next_cid++) << 16);
}

static int nvme_create_io_queues(void) {
    nvme_sqe_t sqe;

    /*
     * Completion queue first: the submission queue references it by ID, so
     * creating the SQ first is a protocol error.
     *
     * cdw10: QID in 15:0, QSIZE-1 in 31:16.
     * cdw11: PC in bit 0, IEN in bit 1, IV in 31:16.
     * IEN stays clear -- we poll, and an enabled interrupt with no registered
     * handler would just raise spurious IRQs.
     */
    nvme_prepare(&sqe, NVME_ADMIN_CREATE_IO_CQ);
    sqe.prp1  = (u64)(u32)g_io_cq;
    sqe.cdw10 = ((u32)(NVME_IO_QUEUE_SIZE - 1u) << 16) | NVME_IO_QUEUE_ID;
    sqe.cdw11 = 1u;
    if (nvme_admin_command(&sqe, "create I/O completion queue") != 0) return -1;

    /* cdw11 for the SQ: PC in bit 0, QPRIO in 2:1, CQID in 31:16. */
    nvme_prepare(&sqe, NVME_ADMIN_CREATE_IO_SQ);
    sqe.prp1  = (u64)(u32)g_io_sq;
    sqe.cdw10 = ((u32)(NVME_IO_QUEUE_SIZE - 1u) << 16) | NVME_IO_QUEUE_ID;
    sqe.cdw11 = (NVME_IO_QUEUE_ID << 16) | 1u;
    if (nvme_admin_command(&sqe, "create I/O submission queue") != 0) return -1;

    return 0;
}

static int nvme_identify_namespace(void) {
    nvme_sqe_t sqe;
    const nvme_id_ns_t* ns;
    const u8* lbaf;
    u32 format_index;
    u32 lba_shift;
    u32 lba_size;
    u64 blocks;

    memset(g_identify_buffer, 0, sizeof(g_identify_buffer));

    nvme_prepare(&sqe, NVME_ADMIN_IDENTIFY);
    sqe.nsid  = 1u;
    sqe.prp1  = (u64)(u32)g_identify_buffer;
    sqe.cdw10 = NVME_CNS_NAMESPACE;
    if (nvme_admin_command(&sqe, "identify namespace 1") != 0) return -1;

    ns = (const nvme_id_ns_t*)g_identify_buffer;
    blocks = ns->nsze;
    if (blocks == 0u) {
        serial_writeln("[LastOS] nvme namespace 1 is not active");
        return -1;
    }

    /*
     * Resolve the LBA size instead of assuming 512. Formatted-4K drives are
     * common, and silently treating a 4096-byte LBA as 512 would return one
     * eighth of every sector and corrupt writes.
     */
    format_index = ns->flbas & 0x0Fu;
    if (format_index > ns->nlbaf) {
        serial_writeln("[LastOS] nvme namespace reports an out-of-range LBA format");
        return -1;
    }
    lbaf = g_identify_buffer + sizeof(nvme_id_ns_t) + (format_index * 4u);
    lba_shift = lbaf[2] & 0x3Fu;            /* LBADS, bits 23:16 of the entry */
    if (lba_shift < 9u || lba_shift > 12u) {
        serial_writeln("[LastOS] nvme namespace has an implausible LBA size");
        return -1;
    }
    lba_size = 1u << lba_shift;
    if (lba_size != NVME_LBA_SIZE) {
        serial_write("[LastOS] nvme namespace uses ");
        serial_write_dec(lba_size);
        serial_writeln("-byte LBAs; this driver only supports 512");
        return -1;
    }

    /* The disk layer is 32-bit LBA, so clamp rather than wrap. */
    if (blocks > 0xFFFFFFFFull) {
        serial_writeln("[LastOS] nvme namespace exceeds 32-bit LBA; clamping");
        blocks = 0xFFFFFFFFull;
    }
    g_namespace_blocks = (u32)blocks;
    return 0;
}

static int nvme_io_command(u32 lba, u8 write) {
    nvme_sqe_t sqe;

    nvme_prepare(&sqe, write ? NVME_IO_WRITE : NVME_IO_READ);
    sqe.nsid  = 1u;
    sqe.prp1  = (u64)(u32)g_io_buffer;
    sqe.cdw10 = lba;    /* SLBA low  */
    sqe.cdw11 = 0u;     /* SLBA high -- lba is 32-bit, so always zero */
    sqe.cdw12 = 0u;     /* NLB is 0-based: 0 means one block */

    return nvme_submit_sync(&sqe, g_io_sq, g_io_cq,
                            NVME_IO_QUEUE_SIZE, NVME_IO_QUEUE_ID,
                            &g_io_sq_tail, &g_io_cq_head, &g_io_phase,
                            write ? "write" : "read");
}

/* Reset all queue state and zero the rings. Completion phase tags start at 0,
 * so the first fresh completion carries phase 1. */
static void nvme_reset_queues(void) {
    memset(g_admin_sq, 0, sizeof(g_admin_sq));
    memset(g_admin_cq, 0, sizeof(g_admin_cq));
    memset(g_io_sq, 0, sizeof(g_io_sq));
    memset(g_io_cq, 0, sizeof(g_io_cq));
    g_admin_sq_tail = 0u;
    g_admin_cq_head = 0u;
    g_admin_phase   = 1u;
    g_io_sq_tail    = 0u;
    g_io_cq_head    = 0u;
    g_io_phase      = 1u;
    g_next_cid      = 1u;
}

int nvme_init(void) {
    pci_device_t dev;
    u32 cap_low, cap_high;
    u32 bar_high;
    u32 mqes, mpsmin, dstrd;
    u32 doorbell_span;
    u32 vs, cc;

    g_ready = 0u;

    if (pci_find_class(NVME_CLASS_CODE, NVME_SUBCLASS, NVME_PROG_IF, &dev) != 0) {
        return -1;   /* no controller present; not an error worth logging */
    }

    serial_write("[LastOS] nvme controller at ");
    serial_write_dec(dev.bus);
    serial_putc(':');
    serial_write_dec(dev.slot);
    serial_putc('.');
    serial_write_dec(dev.func);
    serial_putc('\n');

    /* BAR0 must be memory space; the register block is not reachable via I/O ports. */
    if ((dev.bar0 & 1u) != 0u) {
        serial_writeln("[LastOS] nvme BAR0 is an I/O BAR; unsupported");
        return -1;
    }

    /*
     * A 64-bit BAR whose upper dword is set lives above 4 GiB, which this
     * 32-bit kernel cannot map at all. Detect it rather than mapping the
     * truncated low half and writing registers into unrelated memory.
     */
    if (((dev.bar0 >> 1) & 3u) == 2u) {
        bar_high = pci_config_read32(dev.bus, dev.slot, dev.func, 0x14u);
        if (bar_high != 0u) {
            serial_writeln("[LastOS] nvme BAR0 is above 4GiB; unreachable in 32-bit mode");
            return -1;
        }
    }

    g_bar0 = dev.bar0 & 0xFFFFF000u;
    if (g_bar0 == 0u) {
        serial_writeln("[LastOS] nvme BAR0 is unassigned");
        return -1;
    }

    if (pci_enable_bus_master(&dev) != 0) {
        serial_writeln("[LastOS] nvme could not enable bus mastering");
        return -1;
    }

    /* Map the register block uncached before touching any register. */
    if (paging_map_range(g_bar0, g_bar0, 8192u,
                         PAGE_FLAG_RW | PAGE_FLAG_PWT | PAGE_FLAG_PCD) != 0) {
        serial_writeln("[LastOS] nvme failed to map BAR0");
        return -1;
    }

    cap_low  = nvme_read_reg32(NVME_REG_CAP);
    cap_high = nvme_read_reg32(NVME_REG_CAP + 4u);

    /* CAP.MQES (15:0) is 0-based and caps how large our rings may be. */
    mqes = (cap_low & 0xFFFFu) + 1u;
    if (mqes < NVME_ADMIN_QUEUE_SIZE || mqes < NVME_IO_QUEUE_SIZE) {
        serial_write("[LastOS] nvme max queue entries is ");
        serial_write_dec(mqes);
        serial_writeln("; too small for this driver");
        return -1;
    }

    /* CAP.DSTRD is bits 35:32, i.e. bits 3:0 of the high dword. */
    dstrd = cap_high & 0x0Fu;
    g_doorbell_stride = 4u << dstrd;

    /*
     * We mapped 8 KiB from BAR0, and the highest doorbell we touch is the I/O
     * completion queue head. Refuse rather than write past the mapping.
     */
    doorbell_span = NVME_REG_DOORBELL +
                    (2u * NVME_IO_QUEUE_ID + 2u) * g_doorbell_stride;
    if (doorbell_span > 8192u) {
        serial_writeln("[LastOS] nvme doorbell stride too large for the mapped window");
        return -1;
    }

    /* CAP.MPSMIN is bits 51:48, i.e. bits 19:16 of the high dword. We only
     * ever program MPS=0 (4 KiB), so a higher minimum is unusable. */
    mpsmin = (cap_high >> 16) & 0x0Fu;
    if (mpsmin != 0u) {
        serial_writeln("[LastOS] nvme minimum page size exceeds 4KiB; unsupported");
        return -1;
    }

    vs = nvme_read_reg32(NVME_REG_VS);
    serial_write("[LastOS] nvme version ");
    serial_write_dec((vs >> 16) & 0xFFFFu);
    serial_putc('.');
    serial_write_dec((vs >> 8) & 0xFFu);
    serial_putc('.');
    serial_write_dec(vs & 0xFFu);
    serial_putc('\n');

    /* Take the controller down before reprogramming the admin queues. */
    cc = nvme_read_reg32(NVME_REG_CC);
    if ((cc & NVME_CC_EN) != 0u) {
        nvme_write_reg32(NVME_REG_CC, cc & ~NVME_CC_EN);
    }
    if (nvme_wait_ready(0u) != 0) {
        serial_writeln("[LastOS] nvme controller would not go idle");
        return -1;
    }

    /* Rings must be zeroed before the controller can see them, so the initial
     * phase tags read as 0. */
    nvme_reset_queues();

    /* AQA: ASQS-1 in 11:0, ACQS-1 in 27:16. */
    nvme_write_reg32(NVME_REG_AQA,
                     ((u32)(NVME_ADMIN_QUEUE_SIZE - 1u) << 16) |
                     (u32)(NVME_ADMIN_QUEUE_SIZE - 1u));
    nvme_write_reg64(NVME_REG_ASQ, (u64)(u32)g_admin_sq);
    nvme_write_reg64(NVME_REG_ACQ, (u64)(u32)g_admin_cq);

    cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K |
         NVME_CC_AMS_RR | NVME_CC_SHN_NONE | NVME_CC_IOSQES | NVME_CC_IOCQES;
    nvme_write_reg32(NVME_REG_CC, cc);

    if (nvme_wait_ready(1u) != 0) {
        serial_writeln("[LastOS] nvme controller would not become ready");
        return -1;
    }

    if (nvme_create_io_queues() != 0) return -1;
    if (nvme_identify_namespace() != 0) return -1;

    serial_write("[LastOS] nvme namespace 1: ");
    serial_write_dec(g_namespace_blocks);
    serial_write(" sectors, ");
    serial_write_dec(g_namespace_blocks / 2048u);
    serial_writeln(" MiB");

    g_ready = 1u;
    return 0;
}

int nvme_ready(void) {
    return g_ready ? 1 : 0;
}

u32 nvme_sector_count(void) {
    return g_ready ? g_namespace_blocks : 0u;
}

int nvme_read_sector(u32 lba, u8* out512) {
    if (!g_ready || !out512 || lba >= g_namespace_blocks) return -1;
    memset(g_io_buffer, 0, NVME_LBA_SIZE);
    if (nvme_io_command(lba, 0u) != 0) return -1;
    memcpy(out512, g_io_buffer, NVME_LBA_SIZE);
    return 0;
}

int nvme_write_sector(u32 lba, const u8* in512) {
    if (!g_ready || !in512 || lba >= g_namespace_blocks) return -1;
    memcpy(g_io_buffer, in512, NVME_LBA_SIZE);
    return nvme_io_command(lba, 1u);
}
