#include "virtio_ring.h"
#include "paging.h"
#include "string.h"

static u32 align_up(u32 value, u32 align) {
    if (align == 0u) return value;
    return (value + align - 1u) & ~(align - 1u);
}

static void ring_barrier(void) {
    __asm__ __volatile__("" ::: "memory");
}

static int ring_need_event(u16 event_idx, u16 new_idx, u16 old_idx) {
    return (u16)(new_idx - event_idx - 1u) < (u16)(new_idx - old_idx);
}

u32 virtio_ring_size(u16 num, u32 align) {
    u32 avail_end = ((u32)num * sizeof(virtio_ring_desc_t)) + 4u +
                    ((u32)num * sizeof(u16)) + sizeof(u16);
    u32 used_start = align_up(avail_end, align);
    return used_start + 4u + ((u32)num * sizeof(virtio_ring_used_elem_t)) + sizeof(u16);
}

int virtio_ring_init(virtio_ring_t* ring, void* memory, u16 num, u32 align) {
    u8* base;
    u32 avail_end;
    if (!ring || !memory || num == 0u || (num & (num - 1u)) != 0u || align == 0u) return -1;
    base = (u8*)memory;
    memset(memory, 0, virtio_ring_size(num, align));
    ring->num = num;
    ring->free_head = 0u;
    ring->num_free = num;
    ring->last_used = 0u;
    ring->desc = (virtio_ring_desc_t*)base;
    ring->avail_flags = (u16*)(base + ((u32)num * sizeof(virtio_ring_desc_t)));
    ring->avail_idx = ring->avail_flags + 1u;
    ring->avail_ring = ring->avail_idx + 1u;
    ring->avail_used_event = ring->avail_ring + num;
    avail_end = (u32)((u8*)ring->avail_used_event + sizeof(u16) - base);
    ring->used_flags = (u16*)(base + align_up(avail_end, align));
    ring->used_idx = ring->used_flags + 1u;
    ring->used_ring = (virtio_ring_used_elem_t*)(ring->used_idx + 1u);
    ring->used_avail_event = (u16*)(ring->used_ring + num);
    for (u16 i = 0u; i < num; i++) {
        ring->desc[i].next = (u16)((i + 1u) < num ? (i + 1u) : 0u);
    }
    return 0;
}

u32 virtio_ring_desc_address(const virtio_ring_t* ring) {
    if (!ring || !ring->desc) return 0u;
    return (u32)paging_virt_to_phys((u64)ring->desc);
}

u32 virtio_ring_avail_address(const virtio_ring_t* ring) {
    if (!ring || !ring->avail_flags) return 0u;
    return (u32)paging_virt_to_phys((u64)ring->avail_flags);
}

u32 virtio_ring_used_address(const virtio_ring_t* ring) {
    if (!ring || !ring->used_flags) return 0u;
    return (u32)paging_virt_to_phys((u64)ring->used_flags);
}

int virtio_ring_add_chain(virtio_ring_t* ring, const virtio_ring_buffer_t* buffers,
                          u16 count, u16* out_head) {
    u16 head;
    if (!ring || !buffers || !out_head || count == 0u || count > ring->num_free) return -1;
    head = ring->free_head;
    for (u16 i = 0u; i < count; i++) {
        u16 index = ring->free_head;
        u16 next_free = ring->desc[index].next;
        ring->free_head = next_free;
        ((u32*)&ring->desc[index].addr)[0] = buffers[i].addr_low;
        ((u32*)&ring->desc[index].addr)[1] = buffers[i].addr_high;
        ring->desc[index].len = buffers[i].len;
        ring->desc[index].flags = buffers[i].flags;
        if (i + 1u < count) ring->desc[index].flags |= VIRTIO_RING_DESC_NEXT;
        ring->desc[index].next = (u16)((i + 1u < count) ? ring->free_head : 0u);
    }
    ring->num_free = (u16)(ring->num_free - count);
    *out_head = head;
    return 0;
}

int virtio_ring_publish(virtio_ring_t* ring, u16 head) {
    u16 index;
    if (!ring || ring->num_free >= ring->num) return -1;
    index = (u16)(*ring->avail_idx & (ring->num - 1u));
    ring->avail_ring[index] = head;
    ring_barrier();
    *ring->avail_idx = (u16)(*ring->avail_idx + 1u);
    ring_barrier();
    return 0;
}

int virtio_ring_need_notify(const virtio_ring_t* ring, u16 old_idx, u16 new_idx) {
    if (!ring || !ring->used_flags) return 0;
    ring_barrier();
    if (*ring->used_flags & VIRTIO_RING_USED_NO_NOTIFY) return 0;
    return ring_need_event(ring->used_avail_event ? *ring->used_avail_event : 0u,
                           new_idx, old_idx);
}

void virtio_ring_set_avail_flags(virtio_ring_t* ring, u16 flags) {
    if (!ring || !ring->avail_flags) return;
    *ring->avail_flags = flags;
    ring_barrier();
}

void virtio_ring_set_used_event(virtio_ring_t* ring, u16 event_idx) {
    if (!ring || !ring->avail_used_event) return;
    *ring->avail_used_event = event_idx;
    ring_barrier();
}

int virtio_ring_has_used(const virtio_ring_t* ring) {
    if (!ring || !ring->used_idx) return 0;
    ring_barrier();
    if (*ring->used_idx != ring->last_used) return 1;
    return 0;
}

int virtio_ring_get_used(virtio_ring_t* ring, u32* out_id, u32* out_len) {
    u16 index;
    u32 id;
    u16 desc;
    if (!ring || !virtio_ring_has_used(ring)) return -1;
    index = (u16)(ring->last_used & (ring->num - 1u));
    id = ring->used_ring[index].id;
    if (id >= ring->num) return -1;
    if (out_id) *out_id = id;
    if (out_len) *out_len = ring->used_ring[index].len;
    desc = (u16)id;
    do {
        u16 next = ring->desc[desc].next;
        u16 flags = ring->desc[desc].flags;
        ring->num_free++;
        if ((flags & VIRTIO_RING_DESC_NEXT) == 0u) break;
        desc = next;
        if (ring->num_free >= ring->num) return -1;
    } while (1);
    ring->desc[desc].next = ring->free_head;
    ring->free_head = (u16)id;
    ring->last_used = (u16)(ring->last_used + 1u);
    return 0;
}
