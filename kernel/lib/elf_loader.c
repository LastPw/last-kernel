#include "elf_loader.h"
#include "paging.h"
#include "frame_allocator.h"
#include "scheduler.h"
#include "sandbox.h"
#include "serial.h"
#include "string.h"

#define ELF_PT_LOAD 1u
#define ELF_ET_EXEC 2u
#define ELF_ET_DYN  3u
#define ELF_MACHINE_386 3u
#define ELF_MACHINE_X86_64 62u
#define ELF_PAGE 4096u
/* HW-max: room for 512 processes with multi-MB images and 2 MiB stacks;
   the old 8-page stacks and 32-process table were i386-era minima.  The
   COW bookkeeping arrays are static per process: 8192 pages = 32 MiB of
   addressable user VA per process (512 x 8192 x 16 B = 64 MiB total), which
   keeps the kernel image well under 2 GiB so the -mcmodel=kernel base and
   the .heap reserve never collide. */
#define USER_STACK_PAGES 512u
#define ELF_MAX_PROCESSES 32u
#define ELF_MAX_MAPPED_PAGES 1024u
#define ELF_MAX_HEAP_ALLOCS 1024u
/* Heap grows down from just below the top of the 64-bit user range. */
#define USER_HEAP_BASE 0x0000020000000000ull   /* 2 TiB — inside user VMA [USER_VIRT_BASE, USER_VIRT_LIMIT) */
#define USER_HEAP_LIMIT 0x0000800000000000ull

typedef struct __attribute__((packed)) {
    u8 ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u32 entry;
    u32 phoff;
    u32 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
} elf32_header_t;

typedef struct __attribute__((packed)) {
    u32 type;
    u32 offset;
    u32 vaddr;
    u32 paddr;
    u32 filesz;
    u32 memsz;
    u32 flags;
    u32 align;
} elf32_phdr_t;

typedef struct __attribute__((packed)) {
    u8 ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 phoff;
    u64 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
} elf64_header_t;

typedef struct __attribute__((packed)) {
    u32 type;
    u32 flags;
    u64 offset;
    u64 vaddr;
    u64 paddr;
    u64 filesz;
    u64 memsz;
    u64 align;
} elf64_phdr_t;

typedef struct {
    u64 base;
    u32 pages;
    u32 size;
    u8 used;
} elf_heap_alloc_t;

typedef struct {
    u8 used;
    u32 task_id;
    u32 space_id;
    u32 count;
    /* Physical frame addresses.  64-bit: on a machine with more than 4 GiB
       of RAM a frame can sit above the 4 GiB line, and user VAs also live
       above 4 GiB.  Never dereference these directly — frames are only
       reachable through the kernel HHDM alias (paging_phys_to_virt). */
    u64 frames[ELF_MAX_MAPPED_PAGES];
    u64 virtual_pages[ELF_MAX_MAPPED_PAGES];
    u32 page_flags[ELF_MAX_MAPPED_PAGES];
    u8 cow[ELF_MAX_MAPPED_PAGES];
    u64 heap_next;
    u64 entry;
    u64 stack_bottom;
    u64 stack_top;
    u64 stack_guard;
    u64 image_base;
    u64 image_limit;
    elf_heap_alloc_t heap[ELF_MAX_HEAP_ALLOCS];
} elf_process_image_t;

static elf_process_image_t g_images[ELF_MAX_PROCESSES];

static int image_charge_page(const elf_process_image_t* image) {
    if (!image) return -1;
    int token = scheduler_task_sandbox(image->task_id);
    return token >= 0 ? sandbox_charge(token, ELF_PAGE) : -1;
}

static void image_release_page(const elf_process_image_t* image) {
    if (!image) return;
    int token = scheduler_task_sandbox(image->task_id);
    if (token >= 0) sandbox_release(token, ELF_PAGE);
}

static int range_ok(u64 start, u64 length) {
    if (length == 0u || start < USER_VIRT_BASE || start >= USER_VIRT_LIMIT) return 0;
    if (start + length < start || start + length > USER_VIRT_LIMIT) return 0;
    return 1;
}

static elf_process_image_t* image_slot(u32 task_id, int create) {
    int free_slot = -1;
    for (u32 i = 0u; i < ELF_MAX_PROCESSES; i++) {
        if (g_images[i].used && g_images[i].task_id == task_id) return &g_images[i];
        if (!g_images[i].used && free_slot < 0) free_slot = (int)i;
    }
    if (!create || free_slot < 0) return 0;
    memset(&g_images[(u32)free_slot], 0, sizeof(g_images[(u32)free_slot]));
    g_images[(u32)free_slot].used = 1u;
    g_images[(u32)free_slot].task_id = task_id;
    return &g_images[(u32)free_slot];
}

static void image_release(elf_process_image_t* image) {
    if (!image) return;
    (void)paging_space_switch(image->space_id);
    for (u32 i = 0u; i < image->count; i++) {
        (void)paging_unmap_page(image->virtual_pages[i]);
        (void)frame_ref_dec((void*)image->frames[i]);
        image_release_page(image);
    }
    (void)paging_space_switch(PAGING_SPACE_KERNEL);
    (void)paging_space_destroy(image->space_id);
    memset(image, 0, sizeof(*image));
}

int elf32_destroy_user(u32 task_id) {
    elf_process_image_t* image = image_slot(task_id, 0);
    if (!image) return -1;
    image_release(image);
    return 0;
}

int elf32_clone_user(u32 parent_task, u32 child_task, elf_exec_info_t* out) {
    elf_process_image_t* parent = image_slot(parent_task, 0);
    elf_process_image_t* child;
    int space;
    if (!parent || !out || parent->count == 0u) return -1;
    child = image_slot(child_task, 1);
    if (!child) return -1;
    space = paging_space_create(child_task);
    if (space < 0) { memset(child, 0, sizeof(*child)); return -1; }
    child->space_id = (u32)space;
    child->heap_next = parent->heap_next;
    child->entry = parent->entry;
    child->stack_bottom = parent->stack_bottom;
    child->stack_top = parent->stack_top;
    child->stack_guard = parent->stack_guard;
    child->image_base = parent->image_base;
    child->image_limit = parent->image_limit;
    child->count = 0u;
    if (paging_space_switch(parent->space_id) != 0) { image_release(child); return -1; }
    for (u32 i = 0u; i < parent->count; i++) {
        u64 va = parent->virtual_pages[i];
        u32 flags = paging_get_page_flags(va);
        u64 old_frame = paging_get_page_frame(va);
        int writable;
        if ((flags & PAGE_FLAG_PRESENT) == 0u || old_frame == 0u) { image_release(child); return -1; }
        writable = ((flags & PAGE_FLAG_RW) != 0u) || parent->cow[i];
        if (image_charge_page(child) != 0 || frame_ref_inc((void*)old_frame) != 0) {
            image_release_page(child);
            image_release(child);
            return -1;
        }
        if (writable) {
            flags &= ~PAGE_FLAG_RW;
            paging_set_page_flags(va, flags);
            parent->cow[i] = 1u;
            parent->page_flags[i] = flags;
        }
        if (paging_space_switch((u32)space) != 0 ||
            paging_map_page(va, old_frame, flags) != 0) {
            (void)frame_ref_dec((void*)old_frame);
            image_release_page(child);
            image_release(child);
            return -1;
        }
        child->frames[child->count] = old_frame;
        child->virtual_pages[child->count] = va;
        child->page_flags[child->count] = flags;
        child->cow[child->count] = writable ? 1u : 0u;
        child->count++;
        if (paging_space_switch(parent->space_id) != 0) { image_release(child); return -1; }
    }
    for (u32 i = 0u; i < ELF_MAX_HEAP_ALLOCS; i++) child->heap[i] = parent->heap[i];
    (void)paging_space_switch((u32)space);
    memset(out, 0, sizeof(*out));
    out->address_space = (u32)space;
    out->entry = child->entry;
    out->stack_top = child->stack_top;
    out->stack_bottom = child->stack_bottom;
    out->stack_guard = child->stack_guard;
    out->image_base = child->image_base;
    out->image_limit = child->image_limit;
    out->mapped_pages = child->count;
    out->load_segments = 1u;
    (void)paging_space_switch(PAGING_SPACE_KERNEL);
    return 0;
}

int elf32_handle_page_fault(u32 task_id, u64 fault_addr, u32 err_code) {
    elf_process_image_t* image = image_slot(task_id, 0);
    u64 page = fault_addr & ~0xFFFull;
    if (!image) return 0;
    if (paging_space_current() != image->space_id &&
        paging_space_switch(image->space_id) != 0) return 0;
    for (u32 i = 0u; i < image->count; i++) {
        if (image->virtual_pages[i] != page) continue;
        if (!paging_is_mapped(page)) {
            u32 remap_flags = image->page_flags[i] | PAGE_FLAG_PRESENT | PAGE_FLAG_USER;
            if (paging_map_page(page, image->frames[i], remap_flags) != 0) return 0;
            if (!(err_code & 0x2u)) return 1;
        }
        if (!image->cow[i] && !(image->page_flags[i] & PAGE_FLAG_RW)) return 0;
        u64 old_frame = image->frames[i];
        u32 flags = image->page_flags[i] | PAGE_FLAG_RW | PAGE_FLAG_USER | PAGE_FLAG_PRESENT;
        if (image->cow[i] && frame_ref_get((void*)old_frame) > 1u) {
            void* new_frame = frame_alloc();
            if (!new_frame) return 0;
            /* Frames are physical: only reachable through the kernel HHDM
               alias, which every user PML4 clones from the kernel half. */
            memcpy((void*)paging_phys_to_virt((u64)new_frame),
                   (const void*)paging_phys_to_virt(old_frame), ELF_PAGE);
            if (paging_map_page(page, (u64)new_frame, flags) != 0) {
                frame_free(new_frame);
                return 0;
            }
            (void)frame_ref_dec((void*)old_frame);
            image->frames[i] = (u64)new_frame;
        } else {
            paging_set_page_flags(page, flags);
        }
        image->page_flags[i] = flags;
        image->cow[i] = 0u;
        return 1;
    }
    return 0;
}

static int map_segment_page(u64 va, u32 flags, elf_process_image_t* image, u32* mapped) {
    u64 page = va & ~0xFFFull;
    if (paging_is_mapped(page)) {
        serial_write("[elf] map: already mapped va=0x");
        serial_write_hex((u32)(page >> 12));
        serial_putc('\n');
        return -1;
    }
    if (image->count >= ELF_MAX_MAPPED_PAGES) {
        serial_write("[elf] map: page budget exceeded count=");
        serial_write_dec(image->count);
        serial_putc('\n');
        return -1;
    }
    if (!image || image->count >= ELF_MAX_MAPPED_PAGES) return -1;
    if (image_charge_page(image) != 0) {
        serial_writeln("[elf] map: charge failed");
        return -1;
    }
    void* frame = frame_alloc();
    if (!frame) {
        serial_writeln("[elf] map: frame_alloc failed");
        image_release_page(image);
        return -1;
    }
    /* Frame is physical; zero it through the kernel HHDM alias. */
    memset((void*)paging_phys_to_virt((u64)frame), 0, ELF_PAGE);
    if (paging_map_page(page, (u64)frame, flags | PAGE_FLAG_PRESENT | PAGE_FLAG_USER) != 0) {
        serial_writeln("[elf] map: paging_map_page failed");
        serial_write("[elf] map fail va=0x");
        serial_write_hex((u32)(page >> 32));
        serial_write_hex((u32)(page & 0xFFFFFFFFu));
        serial_write(" space=");
        serial_write_dec(paging_space_current());
        serial_write(" flags=0x");
        serial_write_hex(flags);
        serial_write(" free_frames=");
        serial_write_dec(frame_total() - frame_used());
        serial_putc('\n');
        frame_free(frame);
        image_release_page(image);
        return -1;
    }
#ifdef ELF_DEBUG_LOG
    if (image->count <= 30u || (page >> 12) == (0x1000000000ull >> 12)) {
        u64 cr3 = 0;
        __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
        serial_write("[elf] map va=0x");
        serial_write_hex((u32)(page >> 12));
        serial_write(" flags=0x");
        serial_write_hex(flags);
        serial_write(" cnt=");
        serial_write_dec(image->count);
        serial_write(" pgflags=");
        serial_write_hex(paging_get_page_flags(page));
        serial_write(" space=");
        serial_write_dec(paging_space_current());
        serial_write(" cr3=0x");
        serial_write_hex((u32)((cr3 & 0x000FFFFFFFFFF000ull) >> 12));
        serial_putc('\n');
    }
#endif
    image->frames[image->count] = (u64)frame;
    image->virtual_pages[image->count] = page;
    image->page_flags[image->count] = paging_get_page_flags(page);
    image->count++;
    (*mapped)++;
    return 0;
}

typedef struct {
    u64 entry;
    u64 phoff;
    u64 base;
    u64 limit;
    u32 phnum;
    u32 segments;
    u32 phdr_size;
} elf_view_t;

/* Validate a raw ELF image and fill a normalized view.  Both ELF32
   (machine 3) and ELF64 (machine 62) are accepted; ELF64 is the target for
   userland in P3 while the i386 seeds are still bootable. */
static int elf_image_valid(const u8* image, u32 image_len, elf_view_t* v) {
    if (!image || image_len < sizeof(elf32_header_t)) return 0;
    const u8* id = image;
    if (id[0] != 0x7Fu || id[1] != 'E' || id[2] != 'L' || id[3] != 'F') {
        serial_writeln("[LastOS] elf validation: bad magic");
        return 0;
    }
    u64 entry = 0, phoff = 0;
    u32 phnum = 0, phdr_size = 0;
    u32 machine = 0;
    u16 type = 0;
    if (id[4] == 1u) { /* ELF32 */
        if (image_len < (u32)sizeof(elf32_header_t)) return 0;
        const elf32_header_t* h = (const elf32_header_t*)image;
        type = h->type; machine = h->machine;
        entry = h->entry; phoff = h->phoff;
        phnum = h->phnum; phdr_size = sizeof(elf32_phdr_t);
        if (machine != ELF_MACHINE_386 || h->phentsize != sizeof(elf32_phdr_t)) return 0;
    } else if (id[4] == 2u) { /* ELF64 */
        if (image_len < (u32)sizeof(elf64_header_t)) return 0;
        const elf64_header_t* h = (const elf64_header_t*)image;
        type = h->type; machine = h->machine;
        entry = h->entry; phoff = h->phoff;
        phnum = h->phnum; phdr_size = sizeof(elf64_phdr_t);
        if (machine != ELF_MACHINE_X86_64 || h->phentsize != sizeof(elf64_phdr_t)) return 0;
    } else {
        return 0;
    }
    if ((type != ELF_ET_EXEC && type != ELF_ET_DYN) || phnum == 0u || phnum > 32u) {
        serial_write("[LastOS] elf validation: header phentsize=");
        serial_write_dec(phdr_size);
        serial_write(" phnum=");
        serial_write_dec(phnum);
        serial_write(" type=");
        serial_write_dec(type);
        serial_write(" machine=");
        serial_write_dec(machine);
        serial_writeln("");
        return 0;
    }
    if (phoff > image_len || phnum > (image_len - phoff) / phdr_size) {
        serial_writeln("[LastOS] elf validation: program headers outside file");
        return 0;
    }
    if (!range_ok(entry, 1u)) {
        serial_write("[LastOS] elf validation: entry=");
        serial_write_dec(entry);
        serial_writeln(" outside user range");
        return 0;
    }
    u64 base = USER_VIRT_LIMIT, limit = USER_VIRT_BASE, segments = 0u;
    for (u32 i = 0u; i < phnum; i++) {
        const u8* ph = image + phoff + i * phdr_size;
        u32 ptype; u64 pvaddr, poffset, pfilesz, pmemsz;
        if (phdr_size == sizeof(elf32_phdr_t)) {
            const elf32_phdr_t* p = (const elf32_phdr_t*)ph;
            ptype = p->type; pvaddr = p->vaddr; poffset = p->offset;
            pfilesz = p->filesz; pmemsz = p->memsz;
        } else {
            const elf64_phdr_t* p = (const elf64_phdr_t*)ph;
            ptype = p->type; pvaddr = p->vaddr; poffset = p->offset;
            pfilesz = p->filesz; pmemsz = p->memsz;
        }
        if (ptype != ELF_PT_LOAD) continue;
        if (pmemsz == 0u || pfilesz > pmemsz || poffset > image_len ||
            pfilesz > image_len - poffset || !range_ok(pvaddr, pmemsz)) {
            serial_write("[LastOS] elf validation: segment vaddr=");
            serial_write_dec(pvaddr);
            serial_write(" filesz=");
            serial_write_dec(pfilesz);
            serial_write(" memsz=");
            serial_write_dec(pmemsz);
            serial_writeln(" invalid");
            return 0;
        }
        if (pvaddr < base) base = pvaddr;
        if (pvaddr + pmemsz > limit) limit = pvaddr + pmemsz;
        segments++;
    }
    if (segments == 0u || !range_ok(base, limit - base)) return 0;
    v->entry = entry;
    v->phoff = phoff;
    v->base = base;
    v->limit = limit;
    v->phnum = phnum;
    v->segments = segments;
    v->phdr_size = phdr_size;
    return 1;
}

u64 elf32_user_alloc(u32 task_id, u32 size) {
    elf_process_image_t* image = image_slot(task_id, 0);
    if (!image || size == 0u) return 0u;
    u32 pages = (size + ELF_PAGE - 1u) / ELF_PAGE;
    if (pages == 0u || pages > ELF_MAX_MAPPED_PAGES) return 0u;
    if (image->heap_next < USER_HEAP_BASE || image->heap_next >= USER_HEAP_LIMIT) {
        image->heap_next = USER_HEAP_BASE;
    }
    u64 base = (image->heap_next + ELF_PAGE - 1u) & ~0xFFFull;
    u64 end = base + pages * ELF_PAGE;
    if (end < base || end > USER_HEAP_LIMIT) return 0u;
    int slot = -1;
    for (u32 i = 0u; i < ELF_MAX_HEAP_ALLOCS; i++) {
        if (!image->heap[i].used) { slot = (int)i; break; }
    }
    if (slot < 0) return 0u;
    if (paging_space_switch(image->space_id) != 0) return 0u;
    u32 mapped = 0u;
    for (u32 i = 0u; i < pages; i++) {
        if (map_segment_page(base + i * ELF_PAGE, PAGE_FLAG_RW, image, &mapped) != 0) {
            for (u32 j = 0u; j < mapped; j++) {
                u64 va = base + j * ELF_PAGE;
                for (u32 k = 0u; k < image->count; k++) {
                    if (image->virtual_pages[k] != va) continue;
                    (void)paging_unmap_page(va);
                    (void)frame_ref_dec((void*)image->frames[k]);
                    image->page_flags[k] = image->page_flags[image->count - 1u];
                    image->cow[k] = image->cow[image->count - 1u];
                    image_release_page(image);
                    image->count--;
                    image->frames[k] = image->frames[image->count];
                    image->virtual_pages[k] = image->virtual_pages[image->count];
                    break;
                }
            }
            (void)paging_space_switch(PAGING_SPACE_KERNEL);
            return 0u;
        }
    }
    image->heap[(u32)slot].used = 1u;
    image->heap[(u32)slot].base = base;
    image->heap[(u32)slot].pages = pages;
    image->heap[(u32)slot].size = size;
    image->heap_next = end;
    (void)paging_space_switch(PAGING_SPACE_KERNEL);
    return base;
}

int elf32_user_free(u32 task_id, u64 ptr) {
    elf_process_image_t* image = image_slot(task_id, 0);
    if (!image || ptr == 0u) return -1;
    u64 base = ptr & ~0xFFFull;
    int slot = -1;
    for (u32 i = 0u; i < ELF_MAX_HEAP_ALLOCS; i++) {
        if (image->heap[i].used && image->heap[i].base == base) { slot = (int)i; break; }
    }
    if (slot < 0 || paging_space_switch(image->space_id) != 0) return -1;
    u32 pages = image->heap[(u32)slot].pages;
    for (u32 i = 0u; i < pages; i++) {
        u64 va = base + i * ELF_PAGE;
        for (u32 k = 0u; k < image->count; k++) {
            if (image->virtual_pages[k] != va) continue;
            (void)paging_unmap_page(va);
            (void)frame_ref_dec((void*)image->frames[k]);
            image->page_flags[k] = image->page_flags[image->count - 1u];
            image->cow[k] = image->cow[image->count - 1u];
            image_release_page(image);
            image->count--;
            image->frames[k] = image->frames[image->count];
            image->virtual_pages[k] = image->virtual_pages[image->count];
            break;
        }
    }
    memset(&image->heap[(u32)slot], 0, sizeof(image->heap[(u32)slot]));
    (void)paging_space_switch(PAGING_SPACE_KERNEL);
    return 0;
}

int elf32_load_user(const u8* image, u32 image_len, u32 task_id, elf_exec_info_t* out) {
    elf_view_t v;
    if (!out || !elf_image_valid(image, image_len, &v)) {
        serial_writeln("[LastOS] elf load: validation failed");
        return -1;
    }
    (void)elf32_destroy_user(task_id);
    elf_process_image_t* owner = image_slot(task_id, 1);
    if (!owner) return -1;
    int space = paging_space_create(task_id);
    if (space < 0) {
        serial_writeln("[LastOS] elf load: address space unavailable");
        memset(owner, 0, sizeof(*owner));
        return -1;
    }
    owner->space_id = (u32)space;
    owner->heap_next = USER_HEAP_BASE;
    if (paging_space_switch((u32)space) != 0) {
        serial_writeln("[elf] space_switch failed");
        image_release(owner);
        return -1;
    }
    if (!elf_image_valid(image, image_len, &v)) {
        serial_writeln("[elf] second validation failed");
        image_release(owner);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->entry = v.entry;
    out->image_base = v.base & ~0xFFFull;
    out->image_limit = (v.limit + ELF_PAGE - 1u) & ~0xFFFull;
    out->load_segments = v.segments;
    out->address_space = (u32)space;
    owner->entry = out->entry;
    owner->image_base = out->image_base;
    owner->image_limit = out->image_limit;

    for (u32 i = 0u; i < v.phnum; i++) {
        const u8* ph = image + v.phoff + i * v.phdr_size;
        u32 ptype, pflags; u64 pvaddr, poffset, pfilesz, pmemsz;
        if (v.phdr_size == sizeof(elf32_phdr_t)) {
            const elf32_phdr_t* p = (const elf32_phdr_t*)ph;
            ptype = p->type; pflags = p->flags; pvaddr = p->vaddr; poffset = p->offset;
            pfilesz = p->filesz; pmemsz = p->memsz;
        } else {
            const elf64_phdr_t* p = (const elf64_phdr_t*)ph;
            ptype = p->type; pflags = p->flags; pvaddr = p->vaddr; poffset = p->offset;
            pfilesz = p->filesz; pmemsz = p->memsz;
        }
        if (ptype != ELF_PT_LOAD) continue;
        /* Always map as RW during load so the initial memcpy can write the
           file contents.  Exec-only pages are tightened after copying. */
        u32 flags = PAGE_FLAG_RW;
        if (pflags & 1u) flags |= PAGE_FLAG_EXEC;
        u64 first = pvaddr & ~0xFFFull;
        u64 last = (pvaddr + pmemsz + ELF_PAGE - 1u) & ~0xFFFull;
        for (u64 va = first; va < last; va += ELF_PAGE) {
            if (map_segment_page(va, flags, owner, &out->mapped_pages) != 0) {
                serial_writeln("[LastOS] elf load: segment mapping failed");
                image_release(owner);
                return -1;
            }
        }
        /* Copy file bytes into the freshly mapped user pages.  Direct user-VA
           access from kernel context is safe here: the user space is the
           active CR3 while loading, and the kernel's upper half is present. */
        if (pfilesz > 0u) {
            memcpy((void*)(uintptr_t)pvaddr, image + poffset, (size_t)pfilesz);
        }
        /* Tighten pages of non-writable segments back to their ELF
           permissions now that the file data is in place. */
        if ((pflags & 2u) == 0u) {
            for (u64 va = first; va < last; va += ELF_PAGE) {
                u32 pf = paging_get_page_flags(va);
                paging_set_page_flags(va, (pf & ~(u32)PAGE_FLAG_RW));
            }
        }
    }

    out->stack_top = USER_VIRT_LIMIT - ELF_PAGE;
    out->stack_bottom = out->stack_top - USER_STACK_PAGES * ELF_PAGE;
    out->stack_guard = out->stack_top + ELF_PAGE;
    owner->stack_bottom = out->stack_bottom;
    owner->stack_top = out->stack_top;
    owner->stack_guard = out->stack_guard;
    /* useresp is initialized to stack_top.  Keep the top page mapped so the
       first user call and ABI wrappers cannot touch the unmapped boundary. */
    for (u64 va = out->stack_bottom; va <= out->stack_top; va += ELF_PAGE) {
        if (map_segment_page(va, PAGE_FLAG_RW, owner, &out->mapped_pages) != 0) {
            serial_writeln("[LastOS] elf load: stack mapping failed");
            image_release(owner);
            return -1;
        }
    }
    (void)paging_space_switch(PAGING_SPACE_KERNEL);
    return 0;
}

int elf32_guard_addr(u32 task_id, u64* out) {
    elf_process_image_t* image = image_slot(task_id, 0);
    if (!image || !out) return -1;
    *out = image->stack_guard;
    return 0;
}
