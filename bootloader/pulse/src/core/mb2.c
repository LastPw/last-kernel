#include "mb2.h"
#include "lib.h"

#define ALIGN8(x) (((x) + 7) & ~(UINTN)7)

static UINT8 *reserve(mb2_builder_t *mb, UINTN n)
{
    if (ALIGN8(mb->len) + n > mb->cap)
        return NULL;
    UINTN start = ALIGN8(mb->len);
    mb->len = start + n;
    return mb->buf + start;
}

static void put32(UINT8 *p, UINT32 v)
{
    p[0] = (UINT8)v;
    p[1] = (UINT8)(v >> 8);
    p[2] = (UINT8)(v >> 16);
    p[3] = (UINT8)(v >> 24);
}

static void put64(UINT8 *p, UINT64 v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (UINT8)(v >> (8 * i));
}

void mb2_init(mb2_builder_t *mb, VOID *buf, UINTN cap)
{
    mb->buf = buf;
    mb->cap = cap;
    mb->len = 8;                /* reserve room for total_size/reserved */
    memset(buf, 0, 8);
}

void mb2_tag_string(mb2_builder_t *mb, UINT32 type, const char *s)
{
    UINTN slen = strlen(s) + 1;
    UINT8 *t = reserve(mb, 8 + slen);
    if (!t)
        return;
    put32(t, type);
    put32(t + 4, (UINT32)(8 + slen));
    memcpy(t + 8, s, slen);
}

void mb2_tag_memmap(mb2_builder_t *mb, const pulse_memmap_t *mm)
{
    /* entries are base u64 | len u64 | type u32 | rsvd u32 = 24 bytes */
    UINTN esz = 24;
    UINT8 *t = reserve(mb, 16 + mm->count * esz);
    if (!t)
        return;
    put32(t, 6);                                /* mmap tag */
    put32(t + 4, (UINT32)(16 + mm->count * esz));
    put32(t + 8, (UINT32)esz);                  /* entry_size */
    put32(t + 12, 0);                           /* entry_version */

    static const UINT32 kmap[] = {
        [PULSE_MEM_USABLE]      = 1,
        [PULSE_MEM_RESERVED]    = 2,
        [PULSE_MEM_ACPI_RECLAIM] = 3,
        [PULSE_MEM_ACPI_NVS]    = 4,
        [PULSE_MEM_MMIO]        = 2,
        [PULSE_MEM_BAD]         = 5,
    };

    UINT8 *e = t + 16;
    for (UINTN i = 0; i < mm->count; i++, e += esz) {
        put64(e, mm->entry[i].base);
        put64(e + 8, mm->entry[i].len);
        put32(e + 16, kmap[mm->entry[i].type]);
        put32(e + 20, 0);
    }
}

UINTN mb2_finish(mb2_builder_t *mb)
{
    UINT8 *end = reserve(mb, 8);                /* end tag: type 0 */
    if (!end)
        return 0;
    put32(end, 0);
    put32(end + 4, 8);

    put32(mb->buf, (UINT32)mb->len);            /* total_size */
    put32(mb->buf + 4, 0);                      /* reserved */
    return mb->len;
}
