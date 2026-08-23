#include "hw_topology.h"
#include "bootinfo.h"
#include "paging.h"
#include "serial.h"
#include "string.h"

#define ACPI_SCAN_START 0x000E0000u
#define ACPI_SCAN_END   0x00100000u
/* UEFI firmware places the ACPI tables above 4 GiB-adjacent memory (QEMU
   OVMF puts the RSDP/XSDT around 512 MiB); the 32-bit-era 64 MiB cap would
   reject every table and defeat ACPI entirely. 4 GiB covers UEFI layouts. */
#define ACPI_MAX_ADDR   0x100000000ull

/* ACPI SDT header (all ACPI tables start with this) */
typedef struct {
    char sig[4];
    u32 length;
    u8 revision;
    u8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;
} __attribute__((packed)) acpi_sdt_t;

typedef struct {
    char sig[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_addr;
} __attribute__((packed)) rsdp_v1_t;

typedef struct {
    char sig[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_addr;
    u32 length;
    u64 xsdt_addr;
    u8 ext_checksum;
    u8 reserved[3];
} __attribute__((packed)) rsdp_v2_t;

typedef struct {
    acpi_sdt_t h;
    u32 entries[1];
} __attribute__((packed)) rsdt_t;

typedef struct {
    acpi_sdt_t h;
    u64 entries[1];
} __attribute__((packed)) xsdt_t;

/* MADT structures */
typedef struct {
    acpi_sdt_t h;
    u32 lapic_addr;
    u32 flags;
    u8 entries[1];
} __attribute__((packed)) madt_t;

typedef struct {
    u8 type;
    u8 len;
} __attribute__((packed)) acpi_sub_t;

/* FADT (Fixed ACPI Description Table) - partial definition with what we need */
typedef struct {
    acpi_sdt_t h;
    u32 firmware_ctrl;
    u32 dsdt;
    u8 reserved1;
    u8 preferred_pm_profile;
    u16 sci_int;
    u32 smi_cmd;
    u8 acpi_enable;
    u8 acpi_disable;
    u8 s4bios_req;
    u8 pstate_cnt;
    u32 pm1a_evt_blk;
    u32 pm1b_evt_blk;
    u32 pm1a_cnt_blk;
    u32 pm1b_cnt_blk;
    u32 pm2_cnt_blk;
    u32 pm_tmr_blk;
    u32 gpe0_blk;
    u32 gpe1_blk;
    u8 pm1_evt_len;
    u8 pm1_cnt_len;
    u8 pm2_cnt_len;
    u8 pm_tmr_len;
    u8 gpe0_blk_len;
    u8 gpe1_blk_len;
    u8 gpe1_base;
    u8 cst_cnt;
    u16 p_lvl2_lat;
    u16 p_lvl3_lat;
    u16 flush_size;
    u16 flush_stride;
    u8 duty_offset;
    u8 duty_width;
    u8 day_alrm;
    u8 mon_alrm;
    u8 century;
    u16 iapc_boot_arch;
    u8 reserved2;
    u32 flags;
    /* Reset register (from FADT rev 2 and later) */
    u8 reset_reg[12];    /* Generic Address Structure */
    u8 reset_value;
    u8 reserved3[3];
    /* X-PM blocks (64-bit, from FADT rev 3 and later) */
    u64 x_firmware_ctrl;
    u64 x_dsdt;
    u64 x_pm1a_evt_blk;
    u64 x_pm1b_evt_blk;
    u64 x_pm1a_cnt_blk;
    u64 x_pm1b_cnt_blk;
    u64 x_pm2_cnt_blk;
    u64 x_pm_tmr_blk;
    u64 x_gpe0_blk;
    u64 x_gpe1_blk;
} __attribute__((packed)) fadt_t;

/* MCFG (PCI Express Memory Mapped Configuration) */
typedef struct {
    acpi_sdt_t h;
    u64 reserved;
    /* Config entries follow: each is 16 bytes */
    u8 entries[1];
} __attribute__((packed)) mcfg_t;

typedef struct {
    u64 base_addr;
    u16 pci_seg_group;
    u8 start_bus;
    u8 end_bus;
    u32 reserved;
} __attribute__((packed)) mcfg_entry_t;

/* HPET table */
typedef struct {
    acpi_sdt_t h;
    u32 hardware_rev_id;
    u8 comparator_count;
    u16 counter_size;
    u8 reserved;
    u16 pci_vendor_id;
    u8 hpet_addr[12];  /* Generic Address Structure */
    u8 hpet_number;
    u16 minimum_tick;
    u8 page_protection;
} __attribute__((packed)) hpet_table_t;

/* FADT IAPC boot arch flags */
#define FADT_IAPC_LEGACY_DEVICES  0x0001u
#define FADT_IAPC_8042            0x0002u
#define FADT_IAPC_NO_MSI          0x0004u
#define FADT_IAPC_NO_PCIE         0x0008u
#define FADT_IAPC_RTC_S4          0x0010u

/* FADT flags */
#define FADT_FLAG_WBINVD          0x00000001u
#define FADT_FLAG_SLP_BUTTON      0x00000002u
#define FADT_FLAG_S4_RTC_WAKE     0x00000100u
#define FADT_FLAG_RESET_REG       0x00000800u

/* PM1 status register bits */
#define PM1_STATUS_PWRBTN         0x0100u
#define PM1_STATUS_SLPBTN         0x0200u
#define PM1_STATUS_WAKE           0x8000u

/* PM1 control register bits */
#define PM1_CNT_SLP_EN           0x2000u
#define PM1_CNT_SLP_TYP_SHIFT    10u
#define PM1_CNT_SLP_TYP_MASK     0x1C00u

/* SLP_TYPx values for S5 (soft-off) */
#define SLP_TYP_S5               0x07u

/* Status & storage */
static hw_topology_status_t g_status;
static u32 g_apic_id[HW_TOPO_MAX_CORES];
static u32 g_numa_node[HW_TOPO_MAX_CORES];
static u32 g_core_temp[HW_TOPO_MAX_CORES];
static u32 g_madt_phys_addr = 0u;
static u32 g_fadt_phys_addr = 0u;
static u32 g_mcfg_phys_addr = 0u;
static u32 g_hpet_phys_addr = 0u;

/* FADT parsed data for power management */
static u32 g_pm1a_cnt_blk = 0u;
static u32 g_pm1b_cnt_blk = 0u;
static u32 g_pm1a_evt_blk = 0u;
static u32 g_pm1b_evt_blk = 0u;
static u32 g_pm_tmr_blk = 0u;
static u32 g_smi_cmd = 0u;
static u8  g_acpi_enable_val = 0u;
static u8  g_acpi_disable_val = 0u;
static u16 g_slp_typa = 0u;
static u16 g_slp_typb = 0u;

/* MCFG parsed data */
static u32 g_mcfg_base = 0u;
static u8  g_mcfg_end_bus = 0u;

/* HPET parsed data */
static u32 g_hpet_base_from_acpi = 0u;

/* Helper functions */
static int sig_eq(const char* a, const char* b, u32 n) {
    for (u32 i = 0u; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static u8 checksum8(const void* p, u32 len) {
    const u8* b = (const u8*)p;
    u8 sum = 0u;
    for (u32 i = 0u; i < len; i++) sum = (u8)(sum + b[i]);
    return sum;
}

static int phys_ok(u64 addr, u32 len) {
    if (addr == 0u || addr >= ACPI_MAX_ADDR) return 0;
    if (addr + len < addr) return 0;
    if (addr + len > ACPI_MAX_ADDR) return 0;
    return 1;
}

static void cpuid_local(u32 leaf, u32 subleaf, u32* a, u32* b, u32* c, u32* d) {
    __asm__ __volatile__("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(subleaf));
}

/* Map a physical ACPI address through the Limine HHDM direct map.  Long mode
   has no identity map below 4 GiB: Limine (base revision >= 1) unmaps the low
   pages before the kernel runs, so any access to raw physical ACPI/MMIO
   addresses would page fault.  Limine only maps HHDM entries of selected
   memmap types, and the ACPI tables often sit inside reserved/NVS windows it
   skips (QEMU/OVMF puts the RSDP/XSDT around 512 MiB in a reserved window),
   so the containing page is mapped on demand before the first access.
   Note: the legacy RSDP scan window (EBDA) is not part of any memmap entry,
   either, so its pages are missing from the HHDM too — under UEFI the RSDP
   must come from bootinfo_rsdp() (Limine reads it from the EFI configuration
   table), and even the legacy scan below relies on the on-demand mapping. */
static const void* acpi_to_virt(u64 pa) {
    u64 va = pa + g_hhdm_offset;
    if (paging_virt_to_phys(va) == 0) {
        if (paging_map_page(va, pa, PAGE_FLAG_PRESENT | PAGE_FLAG_RW) != 0) return 0;
    }
    return (const void*)va;
}

static const rsdp_v2_t* find_rsdp(void) {
    for (u32 p = ACPI_SCAN_START; p < ACPI_SCAN_END; p += 16u) {
        const rsdp_v1_t* r = (const rsdp_v1_t*)acpi_to_virt(p);
        if (!sig_eq(r->sig, "RSD PTR ", 8u)) continue;
        if (checksum8(r, 20u) != 0u) continue;
        return (const rsdp_v2_t*)acpi_to_virt(p);
    }
    return 0;
}

static const acpi_sdt_t* checked_sdt(u64 addr, const char* sig) {
    if (!phys_ok(addr, sizeof(acpi_sdt_t))) return 0;
    const acpi_sdt_t* h = (const acpi_sdt_t*)acpi_to_virt((u32)addr);
    if (h->length < sizeof(acpi_sdt_t) || !phys_ok(addr, h->length)) return 0;
    if (!sig_eq(h->sig, sig, 4u)) return 0;
    if (checksum8(h, h->length) != 0u) return 0;
    return h;
}

static void add_core(u32 apic_id) {
    if (g_status.core_count >= HW_TOPO_MAX_CORES) return;
    for (u32 i = 0u; i < g_status.core_count; i++) if (g_apic_id[i] == apic_id) return;
    g_apic_id[g_status.core_count] = apic_id;
    g_numa_node[g_status.core_count] = 0u;
    g_core_temp[g_status.core_count] = g_status.thermal_sensor ? 43u : 48u;
    g_status.core_count++;
}

static void parse_madt(const madt_t* m, u32 addr) {
    if (!m) return;
    g_status.madt_found = 1u;
    g_madt_phys_addr = addr;
    g_status.lapic_addr = m->lapic_addr;

    u32 off = sizeof(madt_t) - 1u;
    while (off + sizeof(acpi_sub_t) <= m->h.length) {
        const acpi_sub_t* s = (const acpi_sub_t*)((const u8*)m + off);
        if (s->len < sizeof(acpi_sub_t) || off + s->len > m->h.length) break;
        if (s->type == 0u && s->len >= 8u) {
            const u8* e = (const u8*)s;
            u8 apic = e[3];
            u32 flags = *(const u32*)(e + 4);
            if (flags & 1u) add_core(apic);
        } else if (s->type == 9u && s->len >= 16u) {
            const u8* e = (const u8*)s;
            u32 x2apic = *(const u32*)(e + 4);
            u32 flags = *(const u32*)(e + 8);
            if (flags & 1u) add_core(x2apic);
        }
        off += s->len;
    }
}

static void parse_fadt(const fadt_t* f, u32 addr) {
    if (!f) return;
    g_status.fadt_found = 1u;
    g_fadt_phys_addr = addr;

    /* Extract power management register blocks */
    g_pm1a_cnt_blk = f->pm1a_cnt_blk;
    g_pm1b_cnt_blk = f->pm1b_cnt_blk;
    g_pm1a_evt_blk = f->pm1a_evt_blk;
    g_pm1b_evt_blk = f->pm1b_evt_blk;
    g_pm_tmr_blk   = f->pm_tmr_blk;
    g_smi_cmd      = f->smi_cmd;
    g_acpi_enable_val  = f->acpi_enable;
    g_acpi_disable_val = f->acpi_disable;

    /* Compute SLP_TYP values for S5 shutdown
       SLP_TYP is in bits 12:10 of PM1a_CNT and PM1b_CNT
       Standard S5 value = 0x07, but we read from DSDT ideally.
       For now use standard values. */
    g_slp_typa = (PM1_CNT_SLP_EN | (SLP_TYP_S5 << PM1_CNT_SLP_TYP_SHIFT));
    g_slp_typb = (PM1_CNT_SLP_EN | (SLP_TYP_S5 << PM1_CNT_SLP_TYP_SHIFT));

    if (g_pm1a_cnt_blk) {
        serial_write("[ACPI] FADT: PM1a_cnt=0x");
        serial_write_hex(g_pm1a_cnt_blk);
        serial_write(" PM1b_cnt=0x");
        serial_write_hex(g_pm1b_cnt_blk);
        serial_writeln("");
    }
}

static void parse_mcfg(const mcfg_t* m, u32 addr) {
    if (!m) return;
    g_status.mcfg_found = 1u;
    g_mcfg_phys_addr = addr;

    /* Entries start after the reserved field (8 bytes after header) */
    u32 off = sizeof(acpi_sdt_t) + 8u;
    while (off + sizeof(mcfg_entry_t) <= m->h.length) {
        const mcfg_entry_t* e = (const mcfg_entry_t*)((const u8*)m + off);
        if (e->base_addr < ACPI_MAX_ADDR) {
            g_mcfg_base = (u32)e->base_addr;
            g_mcfg_end_bus = e->end_bus;
            serial_write("[ACPI] MCFG: base=0x");
            serial_write_hex(g_mcfg_base);
            serial_write(" buses=0-");
            serial_write_dec(g_mcfg_end_bus);
            serial_writeln("");
            break;  /* Use first entry only */
        }
        off += sizeof(mcfg_entry_t);
    }
}

static void parse_hpet_tbl(const hpet_table_t* h, u32 addr) {
    if (!h) return;
    g_hpet_phys_addr = addr;

    /* HPET base address is in Generic Address Structure (12 bytes) */
    /* Byte 4-7 is the base address (32-bit) */
    u32 hpet_base = *(const u32*)(h->hpet_addr + 4u);
    if (hpet_base) {
        g_hpet_base_from_acpi = hpet_base;
        serial_write("[ACPI] HPET: base=0x");
        serial_write_hex(hpet_base);
        serial_writeln("");
    }
}

static void set_core_numa(u32 apic_id, u32 node) {
    for (u32 i = 0u; i < g_status.core_count; i++) {
        if (g_apic_id[i] == apic_id) {
            g_numa_node[i] = node;
            if (node + 1u > g_status.numa_nodes) g_status.numa_nodes = node + 1u;
            return;
        }
    }
}

static void parse_srat(const acpi_sdt_t* h) {
    if (!h || h->length < sizeof(acpi_sdt_t) + 12u) return;
    g_status.srat_found = 1u;
    u32 off = sizeof(acpi_sdt_t) + 12u;
    while (off + sizeof(acpi_sub_t) <= h->length) {
        const acpi_sub_t* s = (const acpi_sub_t*)((const u8*)h + off);
        if (s->len < sizeof(acpi_sub_t) || off + s->len > h->length) break;
        if (s->type == 0u && s->len >= 16u) {
            const u8* e = (const u8*)s;
            u32 prox = e[2] | ((u32)e[9] << 8) | ((u32)e[10] << 16) | ((u32)e[11] << 24);
            u32 apic = e[3];
            u32 flags = *(const u32*)(e + 4);
            if (flags & 1u) set_core_numa(apic, prox % HW_TOPO_MAX_NUMA);
        } else if (s->type == 2u && s->len >= 24u) {
            const u8* e = (const u8*)s;
            u32 prox = *(const u32*)(e + 2);
            u32 x2apic = *(const u32*)(e + 8);
            u32 flags = *(const u32*)(e + 12);
            if (flags & 1u) set_core_numa(x2apic, prox % HW_TOPO_MAX_NUMA);
        }
        off += s->len;
    }
    if (g_status.numa_nodes == 0u) g_status.numa_nodes = 1u;
}

static void walk_rsdt(const acpi_sdt_t* root, int xsdt) {
    if (!root) return;
    u32 ent_sz = xsdt ? 8u : 4u;
    u32 count = (root->length - sizeof(acpi_sdt_t)) / ent_sz;
    for (u32 i = 0u; i < count; i++) {
        u64 addr;
        if (xsdt) addr = ((const xsdt_t*)root)->entries[i];
        else addr = ((const rsdt_t*)root)->entries[i];
        if (!phys_ok(addr, sizeof(acpi_sdt_t))) continue;
        const acpi_sdt_t* h = (const acpi_sdt_t*)acpi_to_virt((u32)addr);

        if (sig_eq(h->sig, "APIC", 4u)) {
            const acpi_sdt_t* ok = checked_sdt(addr, "APIC");
            if (ok) parse_madt((const madt_t*)ok, (u32)addr);
        } else if (sig_eq(h->sig, "SRAT", 4u)) {
            const acpi_sdt_t* ok = checked_sdt(addr, "SRAT");
            if (ok) parse_srat(ok);
        } else if (sig_eq(h->sig, "FACP", 4u)) {
            const acpi_sdt_t* ok = checked_sdt(addr, "FACP");
            if (ok) parse_fadt((const fadt_t*)ok, (u32)addr);
        } else if (sig_eq(h->sig, "MCFG", 4u)) {
            const acpi_sdt_t* ok = checked_sdt(addr, "MCFG");
            if (ok) parse_mcfg((const mcfg_t*)ok, (u32)addr);
        } else if (sig_eq(h->sig, "HPET", 4u)) {
            const acpi_sdt_t* ok = checked_sdt(addr, "HPET");
            if (ok) parse_hpet_tbl((const hpet_table_t*)ok, (u32)addr);
        }
    }
}

void hw_topology_init(const cpu_info_t* cpu) {
    for (u32 i = 0u; i < HW_TOPO_MAX_CORES; i++) {
        g_apic_id[i] = i;
        g_numa_node[i] = 0u;
        g_core_temp[i] = 48u;
    }

    /* Zero all status */
    memset(&g_status, 0, sizeof(g_status));
    g_status.numa_nodes = 1u;

    /* Clear ACPI table addresses */
    g_madt_phys_addr = 0u;
    g_fadt_phys_addr = 0u;
    g_mcfg_phys_addr = 0u;
    g_hpet_phys_addr = 0u;
    g_pm1a_cnt_blk = 0u;
    g_pm1b_cnt_blk = 0u;
    g_pm1a_evt_blk = 0u;
    g_pm1b_evt_blk = 0u;
    g_pm_tmr_blk = 0u;
    g_smi_cmd = 0u;
    g_acpi_enable_val = 0u;
    g_acpi_disable_val = 0u;
    g_slp_typa = 0u;
    g_slp_typb = 0u;
    g_mcfg_base = 0u;
    g_mcfg_end_bus = 0u;
    g_hpet_base_from_acpi = 0u;

    if (cpu && cpu->has_cpuid && cpu->max_basic_leaf >= 6u) {
        u32 a, b, c, d;
        cpuid_local(6u, 0u, &a, &b, &c, &d);
        g_status.thermal_sensor = (a & 1u) ? 1u : 0u;
    }

    const rsdp_v2_t* rsdp = 0;
    u64 rsdp_pa = bootinfo_rsdp();
    serial_write("[ACPI] rsdp_pa=0x");
    serial_write_hex((u32)rsdp_pa);
    serial_writeln("");
    if (rsdp_pa) {
        /* Limine (base revision 3) only maps memmap entries of selected types
           through the HHDM; the RSDP response address may sit in a window it
           did not map.  Map the containing page explicitly so the signature
           probe below cannot fault.  Missing PTEs are created from the kernel
           pool; if the page is already present the call is a no-op. */
        {
            int mrc = paging_map_page(rsdp_pa + g_hhdm_offset, rsdp_pa, PAGE_FLAG_PRESENT | PAGE_FLAG_RW);
            serial_write("[ACPI] map_page rc=");
            serial_write_dec((u32)mrc);
            serial_writeln("");
        }
        rsdp = (const rsdp_v2_t*)acpi_to_virt(rsdp_pa);
        if (!sig_eq(rsdp->sig, "RSD PTR ", 8u) || checksum8(rsdp, 20u) != 0u) {
            rsdp = 0;
        }
    }
    if (!rsdp) {
        /* Legacy BIOS fallback: scan the EBDA window.  Only safe when those
           pages are mapped (BIOS boot); under UEFI this window is unmapped
           and the scan would fault, so the Limine-reported RSDP must not be
           zero for ACPI to work there. */
        rsdp = find_rsdp();
    }
    if (rsdp) {
        g_status.acpi_found = 1u;
        serial_writeln("[ACPI] RSDP found");

        if (rsdp->revision >= 2u && rsdp->length >= sizeof(rsdp_v2_t) &&
            checksum8(rsdp, rsdp->length) == 0u && phys_ok(rsdp->xsdt_addr, sizeof(acpi_sdt_t))) {
            const acpi_sdt_t* xsdt = checked_sdt(rsdp->xsdt_addr, "XSDT");
            walk_rsdt(xsdt, 1);
        }

        /* Fallback to RSDT if XSDT not found or MADT not yet found */
        if (!g_status.madt_found && phys_ok(rsdp->rsdt_addr, sizeof(acpi_sdt_t))) {
            const acpi_sdt_t* rsdt = checked_sdt(rsdp->rsdt_addr, "RSDT");
            walk_rsdt(rsdt, 0);
        }

        /* Also walk RSDT for FADT/MCFG/HPET if XSDT didn't provide them */
        if ((!g_status.fadt_found || !g_mcfg_base) && phys_ok(rsdp->rsdt_addr, sizeof(acpi_sdt_t))) {
            const acpi_sdt_t* rsdt = checked_sdt(rsdp->rsdt_addr, "RSDT");
            if (rsdt) walk_rsdt(rsdt, 0);
        }
    } else {
        serial_writeln("[ACPI] RSDP not found, ACPI unavailable");
    }

    if (g_status.core_count == 0u) {
        u32 n = (cpu && cpu->logical_cores) ? cpu->logical_cores : 1u;
        if (n > HW_TOPO_MAX_CORES) n = HW_TOPO_MAX_CORES;
        for (u32 i = 0u; i < n; i++) add_core(i);
    }
    if (g_status.numa_nodes == 0u) g_status.numa_nodes = 1u;

    serial_write("[ACPI] tables: MADT=");
    serial_write_dec(g_status.madt_found);
    serial_write(" FADT=");
    serial_write_dec(g_status.fadt_found);
    serial_write(" MCFG=");
    serial_write_dec(g_status.mcfg_found);
    serial_writeln("");
}

/* ======== Export functions ======== */

u32 hw_topology_core_count(void) { return g_status.core_count ? g_status.core_count : 1u; }
u32 hw_topology_numa_nodes(void) { return g_status.numa_nodes ? g_status.numa_nodes : 1u; }
u32 hw_topology_core_apic_id(u32 core) {
    if (core >= hw_topology_core_count()) return 0u;
    return g_apic_id[core];
}
u32 hw_topology_core_numa_node(u32 core) {
    if (core >= hw_topology_core_count()) return 0u;
    return g_numa_node[core];
}
u32 hw_topology_core_temperature(u32 core) {
    if (core >= hw_topology_core_count()) return 48u;
    return g_core_temp[core];
}
u32 hw_topology_has_thermal_sensor(void) { return g_status.thermal_sensor; }
void hw_topology_account_core(u32 core, u32 busy, u32 queue_depth) {
    if (core >= hw_topology_core_count()) return;
    u32 t = g_core_temp[core];
    if (busy) {
        t += g_status.thermal_sensor ? 1u : 2u;
        t += queue_depth / 8u;
    } else if (t > (g_status.thermal_sensor ? 38u : 42u)) { t--; }
    if (t > 105u) t = 105u;
    g_core_temp[core] = t;
}
void hw_topology_status(hw_topology_status_t* out) { if (out) *out = g_status; }
u32 hw_topology_madt_addr(void) { return g_madt_phys_addr; }
u32 hw_topology_fadt_addr(void) { return g_fadt_phys_addr; }
u32 hw_topology_mcfg_addr(void) { return g_mcfg_phys_addr; }
u32 hw_topology_hpet_addr(void) { return g_hpet_phys_addr; }
u32 hw_topology_hpet_base(void) { return g_hpet_base_from_acpi; }
const void* hw_topology_madt_virt(void) {
    if (!g_madt_phys_addr) return 0;
    return acpi_to_virt(g_madt_phys_addr);
}

/* ACPI power management */
u32 hw_topology_pm1a_cnt_blk(void)    { return g_pm1a_cnt_blk; }
u32 hw_topology_pm1b_cnt_blk(void)    { return g_pm1b_cnt_blk; }
u32 hw_topology_pm1a_evt_blk(void)    { return g_pm1a_evt_blk; }
u32 hw_topology_pm1b_evt_blk(void)    { return g_pm1b_evt_blk; }
u32 hw_topology_pm_tmr_blk(void)      { return g_pm_tmr_blk; }
u16 hw_topology_slp_typa(void)        { return g_slp_typa; }
u16 hw_topology_slp_typb(void)        { return g_slp_typb; }
u16 hw_topology_smi_cmd(void)         { return (u16)g_smi_cmd; }
u8  hw_topology_acpi_enable(void)     { return g_acpi_enable_val; }
u8  hw_topology_acpi_disable(void)    { return g_acpi_disable_val; }

/* PCIe MMCONFIG */
u32 hw_topology_mcfg_base(void)       { return g_mcfg_base; }
u32 hw_topology_mcfg_end_bus(void)    { return (u32)g_mcfg_end_bus; }
