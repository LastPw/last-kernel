#include "paging.h"
#include "string.h"
#include "serial.h"
#include "bootinfo.h"
#include "serial.h"

#define PAGE_ENTRIES 1024u
/* HW-max: 256 kernel page tables + 256 user address spaces; the old
   64/32/16 pool crowded 2-4 GiB machines. */
#define PAGE_TABLE_POOL 256u
#define USER_SPACE_COUNT 256u
#define USER_SPACE_TABLE_POOL 128u
#define PAGE_SIZE 4096u

#define PAGING_DEBUG 0

u64 g_paging_hhdm_offset = 0;
static u64 g_paging_phys_base = 0;
static u64 g_paging_virt_base = 0;

/* ---------------------------------------------------------------------------
 * Address helpers.
 *
 * Tables owned by this unit live in the kernel image (.bss), so their
 * physical address is g_paging_phys_base + (VMA - g_paging_virt_base).
 * Limine maps the kernel image at its VMA and the whole of physical RAM at
 * the HHDM offset, so both the image VMA and the HHDM alias of any physical
 * frame are always accessible from the kernel half.
 * ------------------------------------------------------------------------- */

static u64 phys_of_ptr(const void* p) {
    return g_paging_phys_base + ((u64)(uintptr_t)p - g_paging_virt_base);
}

static u64 virt_of_phys(u64 phys) {
    return phys + g_paging_hhdm_offset;
}

/* ---------------------------------------------------------------------------
 * Kernel space: the active Limine PML4.
 *
 * In P1 the kernel keeps the bootloader's page tables.  Limine maps the
 * kernel image at its VMA, all usable RAM at the HHDM offset, and (base
 * revision >= 1) the first 4 GiB identity, so the 32-bit-era code that maps
 * MMIO with paging_map_range(va, va, ...) keeps working: those calls just
 * re-write the identity entries.  We never swap CR3 for the kernel space;
 * user spaces (P3) build their own PML4 with the kernel half cloned.
 * ------------------------------------------------------------------------- */

static u64* kernel_pml4(void) {
    u64 cr3 = 0;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return (u64*)virt_of_phys(cr3 & 0x000FFFFFFFFFF000ull);
}

/* ── Per-space pools ─────────────────────────────────────────────────────── */

static u64 kernel_pt_pool[PAGE_TABLE_POOL][PAGE_ENTRIES] __attribute__((aligned(4096)));
static u8 kernel_pt_pool_used[PAGE_TABLE_POOL];

static u64 user_pt_pool[USER_SPACE_COUNT - 1u][USER_SPACE_TABLE_POOL][PAGE_ENTRIES] __attribute__((aligned(4096)));
static u8 user_pt_pool_used[USER_SPACE_COUNT - 1u][USER_SPACE_TABLE_POOL];
static u8 user_space_used[USER_SPACE_COUNT - 1u];
static u32 user_space_owner[USER_SPACE_COUNT - 1u];
static u64 user_pml4s[USER_SPACE_COUNT - 1u][PAGE_ENTRIES] __attribute__((aligned(4096)));

static u32 active_user_idx = 0; /* 0 = kernel space active */

static u32 pdpt_index(u64 va) { return (u32)((va >> 30) & 0x1FFu); }
static u32 pd_index(u64 va)   { return (u32)((va >> 21) & 0x1FFu); }
static u32 pt_index(u64 va)   { return (u32)((va >> 12) & 0x1FFu); }
static u32 pml4_index(u64 va) { return (u32)((va >> 39) & 0x1FFu); }

/* ---------------------------------------------------------------------------
 * Low-level table walk.  Table pointers are VIRTUAL (HHDM alias of the
 * physical table); entries carry PHYSICAL addresses.
 * ------------------------------------------------------------------------- */

/* Get a fresh, zeroed 4 KiB table frame from the active space's pool. */
static u64 alloc_table_frame(void) {
    if (active_user_idx == 0) {
        for (u32 i = 0; i < PAGE_TABLE_POOL; i++) {
            if (!kernel_pt_pool_used[i]) {
                kernel_pt_pool_used[i] = 1;
                memset(&kernel_pt_pool[i], 0, sizeof(kernel_pt_pool[i]));
                return phys_of_ptr(&kernel_pt_pool[i]);
            }
        }
    } else {
        u32 s = active_user_idx - 1u;
        for (u32 i = 0; i < USER_SPACE_TABLE_POOL; i++) {
            if (!user_pt_pool_used[s][i]) {
                user_pt_pool_used[s][i] = 1;
                memset(&user_pt_pool[s][i], 0, sizeof(user_pt_pool[s][i]));
                return phys_of_ptr(&user_pt_pool[s][i]);
            }
        }
    }
    return 0;
}

static int ensure_pdpt(u64 va, u32 flags) {
    u64* pml4 = kernel_pml4();
    u32 l4 = pml4_index(va);
    u32 l3 = pdpt_index(va);

    if ((pml4[l4] & PAGE_FLAG_PRESENT) == 0u) return -1;
    u64 pdpt_phys = pml4[l4] & 0x000FFFFFFFFFF000ull;
    if ((pml4[l4] & PAGE_FLAG_PS) != 0u) return -1; /* 1 GiB huge page */

    u64* pdpt = (u64*)virt_of_phys(pdpt_phys);
    if ((pdpt[l3] & PAGE_FLAG_PRESENT) != 0u) return 0;

    u64 pd_page = alloc_table_frame();
    if (!pd_page) return -1;
    pdpt[l3] = pd_page | (flags & 0xFFFu) | PAGE_FLAG_PRESENT;
    return 0;
}

static int ensure_pd_table(u64 va, u32 flags) {
    /* The user PML4 clones only the boot kernel half; full user ranges like
       l4=1 (0x100000000 and up) have no PML4 entry, so the walk must build
       every missing level instead of failing on a bare l4 entry. */
    u64* pml4 = kernel_pml4();
    u32 l4 = pml4_index(va);
    u32 l3 = pdpt_index(va);
    u32 l2 = pd_index(va);

    if ((pml4[l4] & PAGE_FLAG_PRESENT) == 0u) {
        u64 pdpt_page = alloc_table_frame();
        if (!pdpt_page) return -1;
        pml4[l4] = pdpt_page | (flags & 0xFFFu) | PAGE_FLAG_PRESENT;
    } else if ((pml4[l4] & PAGE_FLAG_PS) != 0u) {
        return -1;
    }

    u64* pdpt = (u64*)virt_of_phys(pml4[l4] & 0x000FFFFFFFFFF000ull);
    if ((pdpt[l3] & PAGE_FLAG_PRESENT) == 0u) {
        u64 pd_page = alloc_table_frame();
        if (!pd_page) return -1;
        pdpt[l3] = pd_page | (flags & 0xFFFu) | PAGE_FLAG_PRESENT;
    } else if ((pdpt[l3] & PAGE_FLAG_PS) != 0u) {
        return -1; /* 1 GiB huge page below */
    }

    u64* pd = (u64*)virt_of_phys(pdpt[l3] & 0x000FFFFFFFFFF000ull);
    if ((pd[l2] & PAGE_FLAG_PRESENT) != 0u) return 0;

    u64 pt_page = alloc_table_frame();
    if (!pt_page) return -1;
    pd[l2] = pt_page | (flags & 0xFFFu) | PAGE_FLAG_PRESENT;
    return 0;
}

/* Map a 4 KiB page in the current space. */
static int map_leaf4k(u64 va, u64 pa, u32 flags) {
    va &= ~0xFFFull;
    pa &= 0x000FFFFFFFFFF000ull;
    if ((flags & PAGE_FLAG_USER) && !(va >= USER_VIRT_BASE && va < USER_VIRT_LIMIT)) {
        return -1;
    }
    if (ensure_pd_table(va, flags) != 0) return -1;

    u64* pml4 = kernel_pml4();
    u32 l4 = pml4_index(va);
    u32 l3 = pdpt_index(va);
    u32 l2 = pd_index(va);
    u32 l1 = pt_index(va);

    u64* pdpt = (u64*)virt_of_phys(pml4[l4] & 0x000FFFFFFFFFF000ull);
    u64* pd = (u64*)virt_of_phys(pdpt[l3] & 0x000FFFFFFFFFF000ull);
    u64* pt = (u64*)virt_of_phys(pd[l2] & 0x000FFFFFFFFFF000ull);

    pt[l1] = pa | (flags & 0xFFFu) | PAGE_FLAG_PRESENT |
             ((flags & PAGE_FLAG_NX) ? PAGE_FLAG_NX : 0ULL);
    if (va >= 0x1000000000ull) {
        serial_write("[pg] map4k va=0x");
        serial_write_hex((u32)(va >> 12));
        serial_write(" flags=0x");
        serial_write_hex(flags);
        serial_write(" cr3=0x");
        serial_write_hex((u32)(paging_cr3() >> 12));
        serial_putc('\n');
    }
    __asm__ __volatile__("invlpg (%0)" : : "r"(va) : "memory");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Public API (64-bit addresses; flags are the low 12 bits + NX).
 * ------------------------------------------------------------------------- */

u64 paging_cr3(void) {
    u64 cr3 = 0;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & 0x000FFFFFFFFFF000ull;
}

u64 paging_phys_to_virt(u64 phys) { return phys + g_paging_hhdm_offset; }

u64 paging_virt_to_phys(u64 virt) {
    /* Kernel image VMA -> physical (Limine maps it at both ends). */
    if (virt >= g_paging_virt_base && virt < 0xFFFFFFFFC0000000ull) {
        return g_paging_phys_base + (virt - g_paging_virt_base);
    }
    u64 cr3 = paging_cr3();
    u64* pm = (u64*)virt_of_phys(cr3);
    u32 l4 = pml4_index(virt);
    if ((pm[l4] & PAGE_FLAG_PRESENT) == 0u) return 0;
    u64 l3e = pm[l4] & 0x000FFFFFFFFFF000ull;
    if (pm[l4] & PAGE_FLAG_PS) return (l3e & 0x000FFFFFC0000000ull);
    u64* pdpt = (u64*)virt_of_phys(l3e);
    u32 l3 = pdpt_index(virt);
    if ((pdpt[l3] & PAGE_FLAG_PRESENT) == 0u) return 0;
    u64 l2e = pdpt[l3] & 0x000FFFFFFFFFF000ull;
    if (pdpt[l3] & PAGE_FLAG_PS) return (l2e & 0x000FFFFFFFC00000ull);
    u64* pd = (u64*)virt_of_phys(l2e);
    u32 l2 = pd_index(virt);
    if ((pd[l2] & PAGE_FLAG_PRESENT) == 0u) return 0;
    u64 l1e = pd[l2] & 0x000FFFFFFFFFF000ull;
    if (pd[l2] & PAGE_FLAG_PS) return (l1e & 0x000FFFFFFFE00000ull);
    u64* pt = (u64*)virt_of_phys(l1e);
    u32 l1 = pt_index(virt);
    if ((pt[l1] & PAGE_FLAG_PRESENT) == 0u) return 0;
    return (pt[l1] & 0x000FFFFFFFFFF000ull);
}

int paging_is_mapped(u64 va) {
    return paging_virt_to_phys(va) != 0;
}

int paging_map_page(u64 virt_addr, u64 phys_addr, u32 flags) {
    return map_leaf4k(virt_addr, phys_addr, flags);
}

int paging_map_range(u64 virt_addr, u64 phys_addr, u64 bytes, u32 flags) {
    u64 off = 0;
    while (off < bytes) {
        if (map_leaf4k(virt_addr + off, phys_addr + off, flags) != 0) return -1;
        off += PAGE_SIZE;
    }
    return 0;
}

int paging_unmap_page(u64 virt_addr) {
    u64 va = virt_addr & ~0xFFFull;
    u64* pml4 = kernel_pml4();
    u32 l4 = pml4_index(va);
    u32 l3 = pdpt_index(va);
    u32 l2 = pd_index(va);
    u32 l1 = pt_index(va);

    if ((pml4[l4] & PAGE_FLAG_PRESENT) == 0u) return -1;
    u64* pdpt = (u64*)virt_of_phys(pml4[l4] & 0x000FFFFFFFFFF000ull);
    if ((pdpt[l3] & PAGE_FLAG_PRESENT) == 0u) return -1;
    if (pdpt[l3] & PAGE_FLAG_PS) return -1;
    u64* pd = (u64*)virt_of_phys(pdpt[l3] & 0x000FFFFFFFFFF000ull);
    if ((pd[l2] & PAGE_FLAG_PRESENT) == 0u) return -1;
    if (pd[l2] & PAGE_FLAG_PS) return -1;
    u64* pt = (u64*)virt_of_phys(pd[l2] & 0x000FFFFFFFFFF000ull);
    pt[l1] = 0;
    __asm__ __volatile__("invlpg (%0)" : : "r"(va) : "memory");
    return 0;
}

u64 paging_get_page_frame(u64 virt_addr) {
    return paging_virt_to_phys(virt_addr);
}

u32 paging_get_page_flags(u64 va) {
    u64* pml4 = kernel_pml4();
    u32 l4 = pml4_index(va);
    if (va >= USER_VIRT_BASE && va < (USER_VIRT_BASE + 0x100000000ull)) {
        serial_write("[gpf] va=0x");
        serial_write_hex((u32)(va >> 12));
        serial_write(" l4=0x");
        serial_write_hex(l4);
        serial_write(" pm4e=0x");
        serial_write_hex((u32)(pml4[l4]));
        serial_write(" hhdm=0x");
        serial_write_hex((u32)(g_paging_hhdm_offset >> 32));
        serial_write_hex((u32)g_paging_hhdm_offset);
        serial_write(" cr3=0x");
        serial_write_hex((u32)(paging_cr3() >> 12));
        serial_putc('\n');
    }
    if ((pml4[l4] & PAGE_FLAG_PRESENT) == 0u) return 0;
    u64* pdpt = (u64*)virt_of_phys(pml4[l4] & 0x000FFFFFFFFFF000ull);
    u32 l3 = pdpt_index(va);
    if (va >= USER_VIRT_BASE && va < (USER_VIRT_BASE + 0x100000000ull)) {
        serial_write("[gpf2] l3=0x");
        serial_write_hex(l3);
        serial_write(" pdpte=0x");
        serial_write_hex((u32)(pdpt[l3]));
        serial_putc('\n');
    }
    if ((pdpt[l3] & PAGE_FLAG_PRESENT) == 0u) return 0;
    if (pdpt[l3] & PAGE_FLAG_PS) return (u32)(pdpt[l3] & 0xFFFu);
    u64* pd = (u64*)virt_of_phys(pdpt[l3] & 0x000FFFFFFFFFF000ull);
    u32 l2 = pd_index(va);
    if (va >= USER_VIRT_BASE && va < (USER_VIRT_BASE + 0x100000000ull)) {
        serial_write("[gpf3] l2=0x");
        serial_write_hex(l2);
        serial_write(" pde=0x");
        serial_write_hex((u32)(pd[l2]));
        serial_write(" pd_v=0x");
        serial_write_hex((u32)(((u64)(uintptr_t)pd) >> 12));
        serial_putc('\n');
    }
    if ((pd[l2] & PAGE_FLAG_PRESENT) == 0u) return 0;
    if (pd[l2] & PAGE_FLAG_PS) return (u32)(pd[l2] & 0xFFFu);
    u64* pt = (u64*)virt_of_phys(pd[l2] & 0x000FFFFFFFFFF000ull);
    u32 l1 = pt_index(va);
    if (va >= USER_VIRT_BASE && va < (USER_VIRT_BASE + 0x100000000ull)) {
        serial_write("[gpf4] l1=0x");
        serial_write_hex(l1);
        serial_write(" pte=0x");
        serial_write_hex((u32)(pt[l1]));
        serial_write(" pt_v=0x");
        serial_write_hex((u32)(((u64)(uintptr_t)pt) >> 12));
        serial_putc('\n');
    }
    if ((pt[l1] & PAGE_FLAG_PRESENT) == 0u) return 0;
    return (u32)(pt[l1] & 0xFFFu);
}

void paging_set_page_flags(u64 virt_addr, u32 flags) {
    u64 va = virt_addr & ~0xFFFull;
    u64* pml4 = kernel_pml4();
    u32 l4 = pml4_index(va);
    u32 l3 = pdpt_index(va);
    u32 l2 = pd_index(va);
    u32 l1 = pt_index(va);

    if ((pml4[l4] & PAGE_FLAG_PRESENT) == 0u) return;
    u64* pdpt = (u64*)virt_of_phys(pml4[l4] & 0x000FFFFFFFFFF000ull);
    if ((pdpt[l3] & PAGE_FLAG_PRESENT) == 0u) return;
    if (pdpt[l3] & PAGE_FLAG_PS) return;
    u64* pd = (u64*)virt_of_phys(pdpt[l3] & 0x000FFFFFFFFFF000ull);
    if ((pd[l2] & PAGE_FLAG_PRESENT) == 0u) return;
    if (pd[l2] & PAGE_FLAG_PS) return;
    u64* pt = (u64*)virt_of_phys(pd[l2] & 0x000FFFFFFFFFF000ull);
    if ((pt[l1] & PAGE_FLAG_PRESENT) == 0u) return;

    pt[l1] &= 0x000FFFFFFFFFF000ull;
    pt[l1] |= (u64)(flags & 0xFFFu);
    if (flags & PAGE_FLAG_NX) pt[l1] |= PAGE_FLAG_NX;
    __asm__ __volatile__("invlpg (%0)" : : "r"(va) : "memory");
}

/* W^X policy: refuse mappings that are simultaneously writable and
   executable (kept as a counter; enforcement is opt-in via paging_set_wx_enforced). */
static u8 wx_enforced = 0;
static u8 nx_capable = 1;
static u32 wx_reject_count = 0;
static u8 ps2mb_enabled = 1;
static u64 user_space_base = USER_VIRT_BASE;
static u64 user_space_limit = USER_VIRT_LIMIT;

static u64 sanitize_map_flags(u64 flags) {
    u64 f = flags & 0xFFFu;
    if (wx_enforced && (f & PAGE_FLAG_EXEC) && (f & PAGE_FLAG_RW)) {
        f &= ~PAGE_FLAG_RW;
        wx_reject_count++;
    }
    return f;
}

void paging_set_wx_enforced(u8 enabled) { wx_enforced = enabled ? 1u : 0u; }
u8 paging_wx_enforced(void) { return wx_enforced; }
void paging_set_nx_capable(u8 enabled) { nx_capable = enabled ? 1u : 0u; }
u8 paging_nx_capable(void) { return nx_capable; }
u32 paging_wx_reject_count(void) { return wx_reject_count; }
int paging_huge_page_supported(void) { return (int)ps2mb_enabled; }

void paging_set_user_space_split(u64 user_base, u64 user_limit) {
    user_space_base = user_base;
    user_space_limit = user_limit;
}

int paging_validate_user_ptr(u64 addr, u64 len, int write_access) {
    if (len == 0u) return 0;
    u64 end = addr + len - 1u;
    if (addr < user_space_base || end >= user_space_limit) return 0;
    u64 page = addr & ~0xFFFull;
    while (page <= end) {
        u32 f = paging_get_page_flags(page);
        if ((f & PAGE_FLAG_PRESENT) != 0u) {
            if (write_access && (f & PAGE_FLAG_RW) == 0u) return 0;
        }
        if (page > end - PAGE_SIZE) break;
        page += PAGE_SIZE;
    }
    return 1;
}

int paging_validate_exec_ptr(u64 addr) {
    if (addr < user_space_base || addr >= user_space_limit) return 0;
    u32 f = paging_get_page_flags(addr & ~0xFFFull);
    if ((f & PAGE_FLAG_PRESENT) == 0u) return 0;
    if (f & PAGE_FLAG_NX) return 0;
    return 1;
}

/* ── Huge pages (2 MiB) ──────────────────────────────────────────────────── */

int paging_map_huge_page(u64 virt_addr, u64 phys_addr, u32 flags) {
    u64 va = virt_addr & ~(HUGE_PAGE_SIZE - 1u);
    u64 pa = phys_addr & ~(HUGE_PAGE_SIZE - 1u);
    if (!ps2mb_enabled) return -1;
    if ((flags & PAGE_FLAG_USER) && !(va >= user_space_base && va < user_space_limit)) {
        return -1;
    }
    if (ensure_pdpt(va, flags) != 0) return -1;

    u64* pml4 = kernel_pml4();
    u32 l4 = pml4_index(va);
    u32 l3 = pdpt_index(va);
    u32 l2 = pd_index(va);

    u64* pdpt = (u64*)virt_of_phys(pml4[l4] & 0x000FFFFFFFFFF000ull);
    if ((pdpt[l3] & PAGE_FLAG_PS) != 0u) return -1;
    u64* pd = (u64*)virt_of_phys(pdpt[l3] & 0x000FFFFFFFFFF000ull);

    pd[l2] = pa | (flags & 0xFFFu) | PAGE_FLAG_PRESENT | PAGE_FLAG_PS |
             ((flags & PAGE_FLAG_NX) ? PAGE_FLAG_NX : 0ULL);
    __asm__ __volatile__("invlpg (%0)" : : "r"(va) : "memory");
    return 0;
}

int paging_unmap_huge_page(u64 virt_addr) {
    u64 va = virt_addr & ~(HUGE_PAGE_SIZE - 1u);
    u64* pml4 = kernel_pml4();
    u32 l4 = pml4_index(va);
    u32 l3 = pdpt_index(va);
    u32 l2 = pd_index(va);

    if ((pml4[l4] & PAGE_FLAG_PRESENT) == 0u) return -1;
    u64* pdpt = (u64*)virt_of_phys(pml4[l4] & 0x000FFFFFFFFFF000ull);
    if ((pdpt[l3] & PAGE_FLAG_PRESENT) == 0u) return -1;
    u64* pd = (u64*)virt_of_phys(pdpt[l3] & 0x000FFFFFFFFFF000ull);
    if ((pd[l2] & PAGE_FLAG_PRESENT) == 0u || (pd[l2] & PAGE_FLAG_PS) == 0u) return -1;

    pd[l2] = 0;
    __asm__ __volatile__("invlpg (%0)" : : "r"(va) : "memory");
    return 0;
}

int paging_is_huge_mapped(u64 virt_addr) {
    u64 va = virt_addr & ~(HUGE_PAGE_SIZE - 1u);
    u64* pml4 = kernel_pml4();
    u32 l4 = pml4_index(va);
    if ((pml4[l4] & PAGE_FLAG_PRESENT) == 0u) return 0;
    u64* pdpt = (u64*)virt_of_phys(pml4[l4] & 0x000FFFFFFFFFF000ull);
    u32 l3 = pdpt_index(va);
    if ((pdpt[l3] & PAGE_FLAG_PRESENT) == 0u) return 0;
    u64* pd = (u64*)virt_of_phys(pdpt[l3] & 0x000FFFFFFFFFF000ull);
    u32 l2 = pd_index(va);
    return ((pd[l2] & PAGE_FLAG_PRESENT) && (pd[l2] & PAGE_FLAG_PS)) ? 1 : 0;
}

/* ── User address spaces (P3 wires real mappings; P1 keeps the shells) ───── */

static u64 kernel_cr3_phys = 0;

int paging_space_create(u32 owner_task) {
    for (u32 i = 0u; i < USER_SPACE_COUNT - 1u; i++) {
        if (user_space_used[i]) continue;
        user_space_used[i] = 1u;
        user_space_owner[i] = owner_task;
        memset(user_pml4s[i], 0, sizeof(user_pml4s[i]));
        for (u32 t = 0; t < USER_SPACE_TABLE_POOL; t++) {
            user_pt_pool_used[i][t] = 0;
        }
        /* Clone the WHOLE kernel PML4 (low 0..255 + high 256..511), not just
           the kernel half: kernel-mode code that runs while CR3 points at
           this user PML4 must still reach the low identity map (VGA text at
           0xB8000, MMIO) that early_mmio_map() put into PML4 entry 0.
           Source is the real kernel PML4 (kernel_cr3_phys), not the
           currently-active CR3: by spawn time CR3 may already be another
           user PML4 whose low entries are user mappings. */
        u64* kpml4 = (u64*)virt_of_phys(kernel_cr3_phys);
        for (u32 e = 0; e < PAGE_ENTRIES; e++) user_pml4s[i][e] = kpml4[e];
        serial_write("[paging] space_create idx=");
        serial_write_dec(i);
        serial_write(" kcr3=0x");
        serial_write_hex((u32)(kernel_cr3_phys >> 12));
        serial_write(" k_l4_0=0x");
        serial_write_hex((u32)(kpml4[0] & 0xFFFFu));
        serial_write(" b8000_phys=0x");
        serial_write_hex((u32)(paging_virt_to_phys(0xB8000ull) >> 12));
        serial_putc('\n');
        return (int)(i + 1u);
    }
    return -1;
}

int paging_space_switch(u32 space_id) {
    if (space_id == PAGING_SPACE_KERNEL) {
        active_user_idx = 0;
        if (kernel_cr3_phys != 0u) {
            __asm__ __volatile__("mov %0, %%cr3" : : "r"(kernel_cr3_phys) : "memory");
        }
    } else {
        u32 idx = space_id - 1u;
        if (idx >= USER_SPACE_COUNT - 1u || !user_space_used[idx]) return -1;
        active_user_idx = space_id;
        __asm__ __volatile__("mov %0, %%cr3" : : "r"(phys_of_ptr(&user_pml4s[idx])) : "memory");
    }
    return 0;
}

int paging_space_destroy(u32 space_id) {
    if (space_id == PAGING_SPACE_KERNEL) return -1;
    u32 idx = space_id - 1u;
    if (idx >= USER_SPACE_COUNT - 1u || !user_space_used[idx]) return -1;
    if (active_user_idx == space_id) {
        active_user_idx = 0;
        if (kernel_cr3_phys != 0u) {
            __asm__ __volatile__("mov %0, %%cr3" : : "r"(kernel_cr3_phys) : "memory");
        }
    }
    user_space_used[idx] = 0u;
    user_space_owner[idx] = 0u;
    return 0;
}

u32 paging_space_current(void) { return active_user_idx; }

u64 paging_space_phys(void) {
    return active_user_idx == 0u ? kernel_cr3_phys
                                 : phys_of_ptr(&user_pml4s[active_user_idx - 1u]);
}

/* ── Init ────────────────────────────────────────────────────────────────── */

void paging_init(void) {
    g_paging_hhdm_offset = g_hhdm_offset;
    g_paging_phys_base = g_kernel_phys_base;
    g_paging_virt_base = g_kernel_virt_base;
    /* Limine hands us CR3 on the kernel PML4; remember it so
       paging_space_switch(PAGING_SPACE_KERNEL) can restore CR3 instead of
       leaving the last user space active. */
    {
        u64 cr3 = 0;
        __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
        kernel_cr3_phys = cr3 & 0x000FFFFFFFFFF000ull;
    }
    wx_enforced = 0;
    nx_capable = 1;
    wx_reject_count = 0;
    ps2mb_enabled = 1;
    user_space_base = USER_VIRT_BASE;
    user_space_limit = USER_VIRT_LIMIT;
    active_user_idx = 0;

#if PAGING_DEBUG
    serial_writeln("[paging] init on Limine PML4");
    serial_writeln("[paging] hhdm offset, phys base, virt base logged");
#endif
}
