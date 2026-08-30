#ifndef PAGING_H
#define PAGING_H

#include "types.h"

/* 4-level paging flags (PTE/PDE/PDPTE/PML4E). */
#define PAGE_FLAG_PRESENT 0x001
#define PAGE_FLAG_RW      0x002
#define PAGE_FLAG_USER    0x004
#define PAGE_FLAG_PWT     0x008
#define PAGE_FLAG_PCD     0x010
#define PAGE_FLAG_PS      0x080  /* Page Size: 2MiB PDE huge page, 1GiB PDPTE */
#define PAGE_FLAG_NX      0x8000000000000000ull
#define PAGE_FLAG_GLOBAL  0x100
#define PAGE_FLAG_EXEC    0x001 /* present bit doubles as executable; kept for W^X checks */

/* 2 MiB large page constants */
#define HUGE_PAGE_SIZE    (2ULL * 1024ULL * 1024ULL)
#define HUGE_PAGE_ALIGN   (HUGE_PAGE_SIZE)
#define HUGE_PAGE_SHIFT   21u

/* Compatibility constants kept for the migration era: the identity map no
   longer covers user land; user space moves above 4 GiB in P3. */
#define KERNEL_VIRT_BASE  0xFFFFFFFF80000000ull
#define USER_VIRT_BASE    0x0000010000000000ull
#define USER_VIRT_LIMIT   0x0000800000000000ull
#define PAGING_SPACE_KERNEL 0u

/* HHDM base: physical-to-virtual conversion for the direct map.  Set from
   the limine hhdm response during paging_init(). */
extern u64 g_paging_hhdm_offset;

/* phys <-> virt helpers using the HHDM direct map. */
u64 paging_phys_to_virt(u64 phys);
u64 paging_virt_to_phys(u64 virt);

void paging_init(void);
u64 paging_cr3(void);
int paging_space_create(u32 owner_task);
int paging_space_switch(u32 space_id);
int paging_space_destroy(u32 space_id);
u32 paging_space_current(void);
u64 paging_space_phys(void);
int paging_map_page(u64 virt_addr, u64 phys_addr, u32 flags);
int paging_map_range(u64 virt_addr, u64 phys_addr, u64 bytes, u32 flags);
int paging_unmap_page(u64 virt_addr);
int paging_is_mapped(u64 virt_addr);
void paging_set_page_flags(u64 virt_addr, u32 flags);
u32 paging_get_page_flags(u64 virt_addr);
u64 paging_get_page_frame(u64 virt_addr);
void paging_set_user_space_split(u64 user_base, u64 user_limit);
int paging_validate_user_ptr(u64 addr, u64 len, int write_access);
int paging_validate_exec_ptr(u64 addr);
void paging_set_wx_enforced(u8 enabled);
u8 paging_wx_enforced(void);
void paging_set_nx_capable(u8 enabled);
u8 paging_nx_capable(void);
u32 paging_wx_reject_count(void);

/* Huge page (2 MiB & 1 GiB) support */
#define GIGA_PAGE_SIZE    (1024ULL * 1024ULL * 1024ULL)
#define GIGA_PAGE_ALIGN   (GIGA_PAGE_SIZE)

int paging_huge_page_supported(void);
int paging_map_huge_page(u64 virt_addr, u64 phys_addr, u32 flags);
int paging_unmap_huge_page(u64 virt_addr);
int paging_is_huge_mapped(u64 virt_addr);
int paging_map_1gb_page(u64 virt_addr, u64 phys_addr, u32 flags);
int paging_unmap_1gb_page(u64 virt_addr);

/* ── x86_64 Hardware Protection Keys (PKEYs / PKU) ── */
#define PAGE_PKEY_SHIFT   59u
#define PAGE_PKEY_MASK    (0xFull << PAGE_PKEY_SHIFT)

u8   paging_pku_supported(void);
void paging_pku_init(void);
int  paging_set_page_domain(u64 virt_addr, u32 domain_id);
int  paging_set_range_domain(u64 virt_addr, u64 bytes, u32 domain_id);
void paging_set_domain_rights(u32 domain_id, u8 disable_access, u8 disable_write);

#endif