#include "memmap.h"
#include "lib.h"

/* Raw UEFI_MEMORY_DESCRIPTOR layout (spec-fixed, little-endian targets):
   0: Type u32 | 4: Pad u32 | 8: PhysicalStart u64
   16: VirtualStart u64 | 24: NumberOfPages u64 | 32: Attribute u64  */
#define DESC_TYPE          0
#define DESC_PHYS_START    8
#define DESC_NUM_PAGES     24
#define DESC_MIN_SIZE      40

static UINT64 rd_le64(const UINT8 *p)
{
    UINT64 v = 0;
    for (UINTN i = 8; i-- > 0; )
        v = (v << 8) | p[i];
    return v;
}

static UINT32 rd_le32(const UINT8 *p)
{
    return (UINT32)p[0] | ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

static pulse_mem_type_t map_uefi_type(UINT32 t)
{
    switch (t) {
    case PULSE_EFI_CONVENTIONAL:   return PULSE_MEM_USABLE;
    case PULSE_EFI_ACPI_RECLAIM:   return PULSE_MEM_ACPI_RECLAIM;
    case PULSE_EFI_ACPI_NVS:       return PULSE_MEM_ACPI_NVS;
    case PULSE_EFI_MMIO:
    case PULSE_EFI_MMIO_PORT:      return PULSE_MEM_MMIO;
    case PULSE_EFI_UNUSABLE:       return PULSE_MEM_BAD;
    /* loader/boot-services/runtime/persistent/unknown: keep off-limits */
    default:                       return PULSE_MEM_RESERVED;
    }
}

UINTN memmap_from_uefi(pulse_memmap_t *out, const UINT8 *buf, UINTN bufsz,
                       UINTN dsize)
{
    out->count = 0;
    if (!buf || dsize < DESC_MIN_SIZE || bufsz < dsize)
        return 0;

    UINTN dropped = 0;
    UINTN n = bufsz / dsize;
    for (UINTN i = 0; i < n; i++) {
        const UINT8 *d = buf + i * dsize;
        UINT32 type = rd_le32(d + DESC_TYPE);
        UINT64 base = rd_le64(d + DESC_PHYS_START);
        UINT64 pages = rd_le64(d + DESC_NUM_PAGES);

        if (pages == 0)
            continue;
        if (out->count == PULSE_MEMMAP_MAX) {
            dropped += n - i;
            break;
        }
        pulse_mem_entry_t *e = &out->entry[out->count++];
        e->base = base;
        e->len = pages * PULSE_UEFI_PAGE;
        e->type = map_uefi_type(type);
    }
    memmap_finalize(out);
    return dropped;
}

void memmap_finalize(pulse_memmap_t *mm)
{
    /* insertion sort by base */
    for (UINTN i = 1; i < mm->count; i++) {
        pulse_mem_entry_t k = mm->entry[i];
        UINTN j = i;
        while (j > 0 && mm->entry[j - 1].base > k.base) {
            mm->entry[j] = mm->entry[j - 1];
            j--;
        }
        mm->entry[j] = k;
    }

    /* merge adjacent equal-type runs */
    UINTN w = 0;
    for (UINTN r = 1; r < mm->count; r++) {
        pulse_mem_entry_t *prev = &mm->entry[w];
        pulse_mem_entry_t *cur = &mm->entry[r];
        if (prev->type == cur->type &&
            prev->base + prev->len == cur->base &&
            cur->len <= ~0ULL - (prev->base + prev->len)) {
            prev->len += cur->len;
        } else {
            mm->entry[++w] = *cur;
        }
    }
    if (mm->count)
        mm->count = w + 1;
}

UINTN memmap_from_e820(pulse_memmap_t *out, const UINT8 *buf,
                       UINTN count, UINTN entry_size)
{
    out->count = 0;
    if (!buf || entry_size < 20)
        return 0;

    static const pulse_mem_type_t kmap[] = {
        [PULSE_E820_USABLE]    = PULSE_MEM_USABLE,
        [PULSE_E820_RESERVED]  = PULSE_MEM_RESERVED,
        [PULSE_E820_ACPI_RECLAIM] = PULSE_MEM_ACPI_RECLAIM,
        [PULSE_E820_ACPI_NVS]  = PULSE_MEM_ACPI_NVS,
        [PULSE_E820_BAD]       = PULSE_MEM_BAD,
    };

    UINTN dropped = 0;
    for (UINTN i = 0; i < count; i++) {
        const UINT8 *d = buf + i * entry_size;
        UINT64 base = rd_le64(d);
        UINT64 len = rd_le64(d + 8);
        UINT32 type = rd_le32(d + 16);
        if (type > 5 || type == 0)
            type = PULSE_E820_RESERVED;
        if (len == 0)
            continue;
        if (out->count == PULSE_MEMMAP_MAX) {
            dropped += count - i;
            break;
        }
        pulse_mem_entry_t *e = &out->entry[out->count++];
        e->base = base;
        e->len = len;
        e->type = kmap[type];
    }
    memmap_finalize(out);
    return dropped;
}

const char *memmap_type_name(pulse_mem_type_t t)
{
    switch (t) {
    case PULSE_MEM_USABLE:      return "usable";
    case PULSE_MEM_RESERVED:    return "reserved";
    case PULSE_MEM_ACPI_RECLAIM: return "acpi-reclaim";
    case PULSE_MEM_ACPI_NVS:    return "acpi-nvs";
    case PULSE_MEM_MMIO:        return "mmio";
    case PULSE_MEM_BAD:         return "bad";
    default:                    return "?";
    }
}
