#include "heap.h"

#define HEAP_ALIGN 8u
/* SECURITY: heap magic values are randomized at init time to resist
 * heap metadata overwrite attacks.  See heap_init(). */
static u32 HEAP_MAGIC_FREE_VAL = 0x48504652u;
static u32 HEAP_MAGIC_USED_VAL = 0x48505553u;
static u32 HEAP_FOOTER_MAGIC_VAL = 0xDEADC0DEu;
#define HEAP_MAGIC_FREE  HEAP_MAGIC_FREE_VAL
#define HEAP_MAGIC_USED  HEAP_MAGIC_USED_VAL
#define HEAP_FOOTER_MAGIC HEAP_FOOTER_MAGIC_VAL
#define HEAP_ALLOC_POISON 0xA5u
#define HEAP_FREE_POISON 0xDDu

typedef struct block_header {
    u32 magic;
    u32 size;
    u32 free;
    u32 reserved;
    struct block_header* next;
} block_header_t;

static block_header_t* heap_head = 0;
static u8* heap_start = 0;
static u8* heap_end = 0;
static u32 used_bytes = 0;
static u32 peak_bytes = 0;
static u32 corruption_count = 0;

static u32 align_up(u32 v) {
    return (v + (HEAP_ALIGN - 1u)) & ~(HEAP_ALIGN - 1u);
}

static u32 align_down(u32 v) {
    return v & ~(HEAP_ALIGN - 1u);
}

static u8* block_payload(block_header_t* b) {
    return ((u8*)b) + sizeof(block_header_t);
}

static u32* block_footer(block_header_t* b) {
    return (u32*)(block_payload(b) + b->size);
}

static u8* block_next_addr(block_header_t* b) {
    return (u8*)block_footer(b) + sizeof(u32);
}

static int ptr_in_heap(const void* p) {
    const u8* v = (const u8*)p;
    return heap_start && heap_end && v >= heap_start && v < heap_end;
}

/* SECURITY: public API for callers (e.g. syscall free) to validate that a
 * pointer falls within the heap before freeing it. */
int heap_contains(const void* ptr) {
    return ptr_in_heap(ptr);
}

static void set_footer(block_header_t* b) {
    u32* f = block_footer(b);
    *f = HEAP_FOOTER_MAGIC ^ b->size ^ b->magic;
}

static int footer_ok(block_header_t* b) {
    u32* f = block_footer(b);
    u32 expect = HEAP_FOOTER_MAGIC ^ b->size ^ b->magic;
    return (*f == expect);
}

static void poison_fill(u8* ptr, u32 len, u8 byte) {
    for (u32 i = 0; i < len; i++) {
        ptr[i] = byte;
    }
}

static int block_header_ok(block_header_t* b) {
    if (!ptr_in_heap(b)) {
        return 0;
    }
    if ((b->magic != HEAP_MAGIC_FREE) && (b->magic != HEAP_MAGIC_USED)) {
        return 0;
    }
    if (b->size == 0u) {
        return 0;
    }
    if (!ptr_in_heap(block_footer(b))) {
        return 0;
    }
    if (!footer_ok(b)) {
        return 0;
    }
    if (b->next && !ptr_in_heap(b->next)) {
        return 0;
    }
    return 1;
}

void heap_init(void* start, u32 size) {
    if (!start || size <= (sizeof(block_header_t) + sizeof(u32) + 16u)) {
        heap_head = 0;
        heap_start = 0;
        heap_end = 0;
        used_bytes = 0;
        peak_bytes = 0;
        corruption_count = 0;
        return;
    }

    heap_start = (u8*)start;
    heap_end = heap_start + size;

    /* SECURITY: randomize magic values using heap address to resist
     * targeted heap metadata overwrites. */
    u32 xor_key = (u32)((size_t)start >> 4) ^ (u32)size;
    HEAP_MAGIC_FREE_VAL = 0x48504652u ^ xor_key;
    HEAP_MAGIC_USED_VAL = 0x48505553u ^ xor_key;
    HEAP_FOOTER_MAGIC_VAL = 0xDEADC0DEu ^ xor_key;

    heap_head = (block_header_t*)heap_start;
    heap_head->magic = HEAP_MAGIC_FREE;
    heap_head->free = 1u;
    heap_head->reserved = 0u;
    heap_head->next = 0;
    heap_head->size = align_down(size - sizeof(block_header_t) - sizeof(u32));
    set_footer(heap_head);

    used_bytes = 0;
    peak_bytes = 0;
    corruption_count = 0;

    poison_fill(block_payload(heap_head), heap_head->size, HEAP_FREE_POISON);
}

void* heap_alloc(size_t req_size) {
    if (!heap_head || req_size == 0u) {
        return 0;
    }

    u32 size = align_up((u32)req_size);
    block_header_t* curr = heap_head;

    while (curr) {
        if (!block_header_ok(curr)) {
            corruption_count++;
            return 0;
        }

        if (curr->free && curr->size >= size) {
            u32 remain = curr->size - size;

            if (remain >= (sizeof(block_header_t) + sizeof(u32) + HEAP_ALIGN)) {
                u32 next_size = align_down(remain - sizeof(block_header_t) - sizeof(u32));
                if (next_size < HEAP_ALIGN) {
                    remain = 0u;
                } else {
                block_header_t* next = (block_header_t*)(block_payload(curr) + size + sizeof(u32));
                next->magic = HEAP_MAGIC_FREE;
                next->free = 1u;
                next->reserved = 0u;
                next->next = curr->next;
                next->size = next_size;
                set_footer(next);
                poison_fill(block_payload(next), next->size, HEAP_FREE_POISON);

                curr->next = next;
                curr->size = size;
                }
            }

            curr->magic = HEAP_MAGIC_USED;
            curr->free = 0u;
            set_footer(curr);
            poison_fill(block_payload(curr), curr->size, HEAP_ALLOC_POISON);

            used_bytes += curr->size;
            if (used_bytes > peak_bytes) {
                peak_bytes = used_bytes;
            }
            return (void*)block_payload(curr);
        }

        curr = curr->next;
    }

    return 0;
}

void heap_free(void* ptr) {
    if (!ptr) {
        return;
    }

    if (!ptr_in_heap(ptr)) {
        corruption_count++;
        return;
    }

    block_header_t* block = (block_header_t*)((u8*)ptr - sizeof(block_header_t));
    if (!block_header_ok(block)) {
        corruption_count++;
        return;
    }

    if (block->free || block->magic != HEAP_MAGIC_USED) {
        corruption_count++;
        return;
    }

    if (used_bytes >= block->size) {
        used_bytes -= block->size;
    } else {
        used_bytes = 0;
    }

    block->magic = HEAP_MAGIC_FREE;
    block->free = 1u;
    set_footer(block);
    poison_fill(block_payload(block), block->size, HEAP_FREE_POISON);

    block_header_t* curr = heap_head;
    while (curr && curr->next) {
        if (!block_header_ok(curr) || !block_header_ok(curr->next)) {
            corruption_count++;
            return;
        }

        if (curr->free && curr->next->free && ((u8*)curr->next == block_next_addr(curr))) {
            curr->size = align_down(curr->size + sizeof(u32) + sizeof(block_header_t) + curr->next->size);
            curr->next = curr->next->next;
            set_footer(curr);
            continue;
        }

        curr = curr->next;
    }
}

u32 heap_used_bytes(void) {
    return used_bytes;
}

u32 heap_peak_bytes(void) {
    return peak_bytes;
}

int heap_validate_integrity(void) {
    if (!heap_head) {
        return -1;
    }

    block_header_t* curr = heap_head;
    u32 guard = 0;

    while (curr) {
        if (!block_header_ok(curr)) {
            corruption_count++;
            return -2;
        }

        if (curr->next && ((u8*)curr->next <= (u8*)curr)) {
            corruption_count++;
            return -3;
        }

        guard++;
        if (guard > 65536u) {
            corruption_count++;
            return -4;
        }

        curr = curr->next;
    }

    return 0;
}

u32 heap_corruption_count(void) {
    return corruption_count;
}

u32 heap_fragmentation_score(void) {
    if (!heap_head) {
        return 100u;
    }

    block_header_t* curr = heap_head;
    u32 total_free = 0u;
    u32 biggest_free = 0u;

    while (curr) {
        if (!block_header_ok(curr)) {
            corruption_count++;
            return 100u;
        }

        if (curr->free) {
            total_free += curr->size;
            if (curr->size > biggest_free) {
                biggest_free = curr->size;
            }
        }

        curr = curr->next;
    }

    if (total_free == 0u) {
        return 0u;
    }

    u32 ext = (biggest_free * 100u) / total_free;
    if (ext >= 100u) {
        return 0u;
    }
    return 100u - ext;
}
