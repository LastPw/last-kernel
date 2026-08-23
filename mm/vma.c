#include "vma.h"
#include "paging.h"
#include "frame_allocator.h"
#include "file.h"
#include "string.h"
#include "spinlock.h"
#include "serial.h"

#define MAX_VMA_AREAS       512u
#define VMA_MMAP_USER_BASE  0x0000000040000000ull

static vma_area_t g_vma_pool[MAX_VMA_AREAS];
static u8 g_vma_used[MAX_VMA_AREAS];
static vma_area_t* g_vma_root = 0;
static spinlock_t g_vma_lock;
static u64 g_mmap_watermark = VMA_MMAP_USER_BASE;

void vma_init(void) {
    spinlock_init(&g_vma_lock);
    memset(g_vma_pool, 0, sizeof(g_vma_pool));
    memset(g_vma_used, 0, sizeof(g_vma_used));
    g_vma_root = 0;
    g_mmap_watermark = VMA_MMAP_USER_BASE;
    serial_writeln("[VMA] Virtual Memory Area & MMAP Subsystem initialized");
}

static vma_area_t* alloc_vma_node(void) {
    for (u32 i = 0u; i < MAX_VMA_AREAS; i++) {
        if (!g_vma_used[i]) {
            g_vma_used[i] = 1u;
            memset(&g_vma_pool[i], 0, sizeof(vma_area_t));
            return &g_vma_pool[i];
        }
    }
    return 0;
}

static void free_vma_node(vma_area_t* node) {
    if (!node) return;
    uintptr_t idx = ((uintptr_t)node - (uintptr_t)g_vma_pool) / sizeof(vma_area_t);
    if (idx < MAX_VMA_AREAS) {
        g_vma_used[idx] = 0u;
    }
}

vma_area_t* vma_create(u32 task_id, u64 start, u64 length, u32 flags) {
    if (length == 0 || (start + length) < start) return 0;
    spinlock_lock(&g_vma_lock);

    vma_area_t* node = alloc_vma_node();
    if (!node) {
        spinlock_unlock(&g_vma_lock);
        return 0;
    }

    node->task_id = task_id;
    node->vm_start = start;
    node->vm_end = start + length;
    node->vm_flags = flags;
    node->color = 0; /* RED */

    if (!g_vma_root) {
        node->color = 1; /* Root is BLACK */
        g_vma_root = node;
    } else {
        vma_area_t* curr = g_vma_root;
        vma_area_t* parent = 0;
        while (curr) {
            parent = curr;
            if (node->vm_start < curr->vm_start) {
                curr = curr->left;
            } else {
                curr = curr->right;
            }
        }
        node->parent = parent;
        if (node->vm_start < parent->vm_start) {
            parent->left = node;
        } else {
            parent->right = node;
        }
    }

    spinlock_unlock(&g_vma_lock);
    return node;
}

vma_area_t* vma_find(u32 task_id, u64 addr) {
    spinlock_lock(&g_vma_lock);
    vma_area_t* curr = g_vma_root;
    while (curr) {
        if (curr->task_id == task_id && addr >= curr->vm_start && addr < curr->vm_end) {
            spinlock_unlock(&g_vma_lock);
            return curr;
        }
        if (addr < curr->vm_start) {
            curr = curr->left;
        } else {
            curr = curr->right;
        }
    }
    spinlock_unlock(&g_vma_lock);
    return 0;
}

static void remove_vma_subtree(vma_area_t* node, u32 task_id) {
    if (!node) return;
    remove_vma_subtree(node->left, task_id);
    remove_vma_subtree(node->right, task_id);
    if (node->task_id == task_id) {
        free_vma_node(node);
    }
}

int vma_destroy_all(u32 task_id) {
    spinlock_lock(&g_vma_lock);
    for (u32 i = 0; i < MAX_VMA_AREAS; i++) {
        if (g_vma_used[i] && g_vma_pool[i].task_id == task_id) {
            g_vma_used[i] = 0;
        }
    }
    g_vma_root = 0;
    spinlock_unlock(&g_vma_lock);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  POSIX MMAP / MUNMAP / MPROTECT Implementation
 * ═══════════════════════════════════════════════════════════════════════ */

u64 vma_mmap(u32 task_id, u64 addr, u64 length, u32 prot, u32 flags, i32 fd, u64 offset) {
    (void)flags;
    if (length == 0) return (u64)-1;

    u64 aligned_len = (length + 4095u) & ~4095u;
    u64 start_va = addr & ~4095u;

    spinlock_lock(&g_vma_lock);
    if (start_va == 0 || start_va < VMA_MMAP_USER_BASE) {
        start_va = g_mmap_watermark;
        g_mmap_watermark += aligned_len + 4096u; /* Add 4KB guard */
    }
    spinlock_unlock(&g_vma_lock);

    /* Construct hardware page flags */
    u32 page_flags = PAGE_FLAG_PRESENT | PAGE_FLAG_USER;
    if (prot & PROT_WRITE) {
        page_flags |= PAGE_FLAG_RW;
    }
    if (!(prot & PROT_EXEC)) {
        page_flags |= (u32)(PAGE_FLAG_NX >> 32); /* NX control */
    }

    /* Map pages and load file contents if file-backed */
    u64 current_va = start_va;
    u64 file_off = offset;

    for (u64 bytes = 0; bytes < aligned_len; bytes += 4096u) {
        void* pphys = frame_alloc();
        if (!pphys) {
            return (u64)-1;
        }

        void* pvirt = (void*)paging_phys_to_virt((u64)(uintptr_t)pphys);
        memset(pvirt, 0, 4096);

        /* If file-backed, read 4KB block from file */
        if (fd >= 0) {
            u32 pos = 0;
            (void)kfile_seek_for_task((u32)fd, (i32)file_off, KSEEK_SET, &pos, task_id);
            (void)kfile_read_for_task((u32)fd, pvirt, 4096, task_id);
            file_off += 4096;
        }

        if (paging_map_page(current_va, (u64)(uintptr_t)pphys, page_flags) != 0) {
            frame_free(pphys);
            return (u64)-1;
        }
        current_va += 4096u;
    }

    /* Create VMA tracking entry */
    vma_area_t* node = vma_create(task_id, start_va, aligned_len, prot);
    if (node) {
        node->fd = fd;
        node->file_offset = offset;
    }

    return start_va;
}

int vma_munmap(u32 task_id, u64 addr, u64 length) {
    if (length == 0) return -1;

    u64 start_va = addr & ~4095u;
    u64 aligned_len = (length + 4095u) & ~4095u;
    u64 current_va = start_va;

    for (u64 bytes = 0; bytes < aligned_len; bytes += 4096u) {
        if (paging_is_mapped(current_va)) {
            u64 pphys = paging_get_page_frame(current_va);
            (void)paging_unmap_page(current_va);
            if (pphys) {
                frame_free((void*)(uintptr_t)pphys);
            }
        }
        current_va += 4096u;
    }

    /* Destroy or update matching VMA */
    vma_area_t* vma = vma_find(task_id, start_va);
    if (vma) {
        free_vma_node(vma);
    }

    return 0;
}

int vma_mprotect(u32 task_id, u64 addr, u64 length, u32 prot) {
    (void)task_id;
    if (length == 0) return -1;

    u64 start_va = addr & ~4095u;
    u64 aligned_len = (length + 4095u) & ~4095u;
    u64 current_va = start_va;

    for (u64 bytes = 0; bytes < aligned_len; bytes += 4096u) {
        if (paging_is_mapped(current_va)) {
            u32 flags = paging_get_page_flags(current_va);
            if (prot & PROT_WRITE) {
                flags |= PAGE_FLAG_RW;
            } else {
                flags &= ~PAGE_FLAG_RW;
            }
            paging_set_page_flags(current_va, flags);
        }
        current_va += 4096u;
    }

    return 0;
}
