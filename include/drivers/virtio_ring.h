#ifndef VIRTIO_RING_H
#define VIRTIO_RING_H

#include "types.h"

#define VIRTIO_RING_DESC_NEXT  0x0001u
#define VIRTIO_RING_DESC_WRITE 0x0002u
#define VIRTIO_RING_DESC_INDIRECT 0x0004u
#define VIRTIO_RING_AVAIL_NO_INTERRUPT 0x0001u
#define VIRTIO_RING_USED_NO_NOTIFY 0x0001u
#define VIRTIO_RING_F_INDIRECT_DESC 28u
#define VIRTIO_RING_F_EVENT_IDX 29u

typedef struct __attribute__((packed)) {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
} virtio_ring_desc_t;

typedef struct __attribute__((packed)) {
    u32 id;
    u32 len;
} virtio_ring_used_elem_t;

typedef struct {
    u32 addr_low;
    u32 addr_high;
    u32 len;
    u16 flags;
} virtio_ring_buffer_t;

typedef struct {
    u16 num;
    u16 free_head;
    u16 num_free;
    u16 last_used;
    virtio_ring_desc_t* desc;
    u16* avail_flags;
    u16* avail_idx;
    u16* avail_ring;
    u16* avail_used_event;
    u16* used_flags;
    u16* used_idx;
    virtio_ring_used_elem_t* used_ring;
    u16* used_avail_event;
} virtio_ring_t;

int virtio_ring_init(virtio_ring_t* ring, void* memory, u16 num, u32 align);
u32 virtio_ring_size(u16 num, u32 align);
u32 virtio_ring_desc_address(const virtio_ring_t* ring);
u32 virtio_ring_avail_address(const virtio_ring_t* ring);
u32 virtio_ring_used_address(const virtio_ring_t* ring);
int virtio_ring_add_chain(virtio_ring_t* ring, const virtio_ring_buffer_t* buffers,
                          u16 count, u16* out_head);
int virtio_ring_publish(virtio_ring_t* ring, u16 head);
int virtio_ring_need_notify(const virtio_ring_t* ring, u16 old_idx, u16 new_idx);
void virtio_ring_set_avail_flags(virtio_ring_t* ring, u16 flags);
void virtio_ring_set_used_event(virtio_ring_t* ring, u16 event_idx);
int virtio_ring_has_used(const virtio_ring_t* ring);
int virtio_ring_get_used(virtio_ring_t* ring, u32* out_id, u32* out_len);

#endif
