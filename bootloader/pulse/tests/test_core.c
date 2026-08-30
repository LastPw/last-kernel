/* Host-side unit tests for src/core. No framework, plain asserts.
   Built and run by `make test-core` (gcc, C11). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "elf.h"
#include "memmap.h"
#include "menu.h"
#include "fat.h"

int fatdbg_dump_dir = 1;

/* mirror of fat.c internal attrs for building test images */
#define T_ATTR_DIRECTORY 0x10

#include "config.h"

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        failures++; \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    unsigned long long _a = (unsigned long long)(a), _b = (unsigned long long)(b); \
    if (_a != _b) { \
        failures++; \
        printf("  FAIL %s:%d: %s == %s (%#llx != %#llx)\n", \
               __FILE__, __LINE__, #a, #b, _a, _b); \
    } \
} while (0)

/* ---------------- config ---------------- */

static void test_config_basic(void)
{
    printf("config: basic\n");
    static const char conf[] =
        "# pulse.conf\r\n"
        "timeout = 5   # seconds\n"
        "\r\n"
        "kernel = /boot/kernel.elf\r\n"
        "cmdline = \"console=ttyS0 quiet\"\n"
        "unknown_future_key = whatever\n";
    pulse_config_t cfg;
    UINTN err_line = 0;
    CHECK_EQ(config_parse(conf, sizeof(conf) - 1, &cfg, &err_line), 0);
    CHECK_EQ(cfg.count, 1);
    CHECK(strcmp(cfg.entry[0].kernel, "/boot/kernel.elf") == 0);
    CHECK(strcmp(cfg.entry[0].cmdline, "console=ttyS0 quiet") == 0);
    CHECK_EQ(cfg.timeout, 5);
}

static void test_config_errors(void)
{
    printf("config: errors\n");
    pulse_config_t cfg;
    UINTN err_line = 0;

    /* missing '=' */
    static const char bad[] = "oops\nkernel=/k\n";
    CHECK_EQ(config_parse(bad, sizeof(bad) - 1, &cfg, &err_line), -1);
    CHECK_EQ(err_line, 1);

    /* empty kernel value */
    static const char empty[] = "kernel=\n";
    CHECK_EQ(config_parse(empty, sizeof(empty) - 1, &cfg, &err_line), -1);

    /* no kernel at all */
    static const char none[] = "timeout=3\n";
    CHECK_EQ(config_parse(none, sizeof(none) - 1, &cfg, &err_line), -2);

    /* timeout overflow */
    static const char big[] = "kernel=/k\ntimeout=99999999999\n";
    CHECK_EQ(config_parse(big, sizeof(big) - 1, &cfg, &err_line), -1);
    CHECK_EQ(err_line, 2);

    /* too many sections */
    char many[2048];
    UINTN off = 0;
    for (int i = 0; i < 10; i++)
        off += snprintf(many + off, sizeof(many) - off,
                        "[e%d]\nkernel=/k%d\n", i, i);
    CHECK_EQ(config_parse(many, off, &cfg, NULL), -1);
}

/* ---------------- elf ---------------- */

/* Simple bump allocator over a fixed arena. */
typedef struct {
    UINT8 *arena;
    UINT64 base;
    UINT64 size;
} arena_t;

static VOID *arena_reserve(VOID *ctx, UINT64 phys, UINT64 len)
{
    arena_t *a = ctx;
    if (phys < a->base || len > a->size || phys - a->base > a->size - len)
        return NULL;
    return a->arena + (UINTN)(phys - a->base);
}

static void put16(UINT8 *p, UINT16 v) { memcpy(p, &v, 2); }
static void put32x(UINT8 *p, UINT32 v) { memcpy(p, &v, 4); }
static void put32(UINT8 *p, UINT32 v) { memcpy(p, &v, 4); }
static void put64(UINT8 *p, UINT64 v) { memcpy(p, &v, 8); }

static void test_elf64(void)
{
    printf("elf64: load two segments\n");
    UINT8 img[512];
    memset(img, 0, sizeof(img));
    memcpy(img, "\x7f" "ELF", 4);
    img[4] = ELF_CLASS64;
    img[5] = ELF_DATA2LSB;
    img[6] = ELF_EV_CURRENT;
    put16(img + 18, EM_X86_64);          /* e_machine */
    put64(img + 24, 0x200000);           /* e_entry */
    put64(img + 32, 64);                 /* e_phoff */
    put16(img + 54, 56);                 /* e_phentsize */
    put16(img + 56, 2);                  /* e_phnum */

    /* phdr[0]: LOAD data at 0x200000 (file data at 192: past both phdrs!) */
    UINT8 *ph = img + 64;
    put32(ph + 0, PT_LOAD);
    put64(ph + 8, 192);                  /* p_offset */
    put64(ph + 16, 0x200000);            /* p_vaddr */
    put64(ph + 32, 16);                  /* p_filesz */
    put64(ph + 40, 16);                  /* p_memsz */
    /* phdr[1]: LOAD bss-only at 0x201000 */
    UINT8 *ph2 = img + 120;
    put32(ph2 + 0, PT_LOAD);
    put64(ph2 + 8, 176);                 /* p_offset */
    put64(ph2 + 16, 0x201000);
    put64(ph2 + 32, 0);                  /* p_filesz */
    put64(ph2 + 40, 48);                 /* p_memsz */

    for (int i = 0; i < 16; i++)
        img[192 + i] = (UINT8)('A' + i);

    static UINT8 arena_mem[0x4000];
    arena_t ar = { arena_mem, 0x200000, sizeof(arena_mem) };
    elf_callbacks_t cb = { arena_reserve };
    UINT64 entry = 0, top = 0;
    CHECK_EQ(elf_load(img, 512, &cb, &ar, &entry, &top), ELF_OK);
    CHECK_EQ(entry, 0x200000);
    CHECK_EQ(top, 0x201030);
    CHECK(arena_mem[0] == 'A' && arena_mem[15] == 'P');
    for (int i = 0; i < 48; i++)
        if (arena_mem[0x1000 + i] != 0) {
            CHECK(!"bss not zeroed");
            break;
        }
}

static void test_elf32(void)
{
    printf("elf32: load one segment\n");
    UINT8 img[256];
    memset(img, 0, sizeof(img));
    memcpy(img, "\x7f" "ELF", 4);
    img[4] = ELF_CLASS32;
    img[5] = ELF_DATA2LSB;
    img[6] = ELF_EV_CURRENT;
    put16(img + 18, EM_386);
    put32(img + 24, 0x100080);           /* e_entry */
    put32(img + 28, 52);                 /* e_phoff */
    put16(img + 42, 32);                 /* e_phentsize */
    put16(img + 44, 1);                  /* e_phnum */

    UINT8 *ph = img + 52;
    put32(ph + 0, PT_LOAD);
    put32(ph + 4, 128);                  /* p_offset */
    put32(ph + 8, 0x100080);             /* p_vaddr */
    put32(ph + 16, 8);                   /* p_filesz */
    put32(ph + 20, 12);                  /* p_memsz (4 bytes bss tail) */
    memcpy(img + 128, "PULSE32!", 8);

    static UINT8 arena_mem[0x2000];
    arena_t ar = { arena_mem, 0x100000, sizeof(arena_mem) };
    elf_callbacks_t cb = { arena_reserve };
    UINT64 entry = 0;
    CHECK_EQ(elf_load(img, sizeof(img), &cb, &ar, &entry, NULL), ELF_OK);
    CHECK_EQ(entry, 0x100080);
    CHECK(memcmp(arena_mem + 0x80, "PULSE32!", 8) == 0);
    for (int i = 8; i < 12; i++)
        CHECK_EQ(arena_mem[0x80 + i], 0);
}

static void test_elf_rejects(void)
{
    printf("elf: rejects\n");
    UINT8 img[128];
    memset(img, 0, sizeof(img));
    memcpy(img, "\x7f" "ELF", 4);
    img[4] = ELF_CLASS64;
    img[5] = ELF_DATA2LSB;
    img[6] = ELF_EV_CURRENT;
    put16(img + 18, EM_X86_64);
    put16(img + 54, 56);                 /* e_phentsize */
    put16(img + 56, 1);                  /* e_phnum = 1 */
    put64(img + 32, 64);                 /* e_phoff */

    arena_t ar = { (UINT8 *)0, 0, 0 };
    elf_callbacks_t cb = { arena_reserve };
    UINT64 entry;

    /* segment table extends past image end */
    CHECK_EQ(elf_load(img, 100, &cb, &ar, &entry, NULL), ELF_E_TRUNC);

    img[0] = 'X';                        /* bad magic */
    CHECK_EQ(elf_load(img, 100, &cb, &ar, &entry, NULL), ELF_E_MAGIC);
    img[0] = 0x7f;

    img[5] = 2;                          /* big-endian */
    CHECK_EQ(elf_load(img, 100, &cb, &ar, &entry, NULL), ELF_E_ENDIAN);
    img[5] = ELF_DATA2LSB;

    img[4] = 9;                          /* bogus class */
    CHECK_EQ(elf_load(img, 100, &cb, &ar, &entry, NULL), ELF_E_CLASS);
}

/* ---------------- memmap ---------------- */

static void wr_desc(UINT8 *p, UINTN dsize, UINT32 type,
                    UINT64 phys, UINT64 pages)
{
    memcpy(p + 0, &type, 4);
    memcpy(p + 8, &phys, 8);
    memcpy(p + 24, &pages, 8);
    (void)dsize;
}

static void test_memmap(void)
{
    printf("memmap: uefi conversion + finalize\n");
    /* deliberately sparse stride (dsize 48 > 40) like real firmware gives */
    UINT8 buf[4 * 48];
    memset(buf, 0xCC, sizeof(buf));      /* pad must be ignored */
    wr_desc(buf + 0 * 48, 48, PULSE_EFI_BS_DATA,      0x0000, 1);
    wr_desc(buf + 1 * 48, 48, PULSE_EFI_CONVENTIONAL, 0x9000, 1);
    wr_desc(buf + 2 * 48, 48, PULSE_EFI_CONVENTIONAL, 0xA000, 2);
    wr_desc(buf + 3 * 48, 48, PULSE_EFI_ACPI_RECLAIM, 0xC000, 1);

    pulse_memmap_t mm;
    CHECK_EQ(memmap_from_uefi(&mm, buf, sizeof(buf), 48), 0);
    CHECK_EQ(mm.count, 3);   /* adjacent usable pair merged */

    CHECK_EQ(mm.entry[0].type, PULSE_MEM_RESERVED);
    CHECK_EQ(mm.entry[0].len, 0x1000);
    CHECK_EQ(mm.entry[1].base, 0x9000);
    CHECK_EQ(mm.entry[1].type, PULSE_MEM_USABLE);
    CHECK_EQ(mm.entry[1].len, 0x3000);   /* merged [0x9000..0xB000) */
    CHECK_EQ(mm.entry[2].base, 0xC000);
    CHECK_EQ(mm.entry[2].type, PULSE_MEM_ACPI_RECLAIM);

    memmap_finalize(&mm);                /* idempotent */
    CHECK_EQ(mm.count, 3);

    /* overflow protection: more descs than fit are dropped and counted */
    UINT8 many[PULSE_MEMMAP_MAX * 48];
    memset(many, 0, sizeof(many));
    for (UINTN i = 0; i < PULSE_MEMMAP_MAX; i++)
        wr_desc(many + i * 48, 48, PULSE_EFI_CONVENTIONAL,
                (i + 1) * 0x10000ULL, 1);
    pulse_memmap_t mm2;
    CHECK_EQ(memmap_from_uefi(&mm2, many, sizeof(many), 48), 0);
    CHECK_EQ(mm2.count, PULSE_MEMMAP_MAX);
}

static void test_memmap_e820(void)
{
    printf("memmap: e820 conversion\n");
    /* three 24-byte-stride entries (ext dword present, ignored) */
    UINT8 buf[3 * 24];
    memset(buf, 0, sizeof(buf));
    struct { UINT64 base, len; UINT32 type, ext; } e;

    e.base = 0x100000;  e.len = 0xE00000; e.type = PULSE_E820_USABLE;
    memcpy(buf + 0 * 24, &e, 24);
    e.base = 0xFFFC0000; e.len = 0x40000;  e.type = PULSE_E820_RESERVED;
    memcpy(buf + 1 * 24, &e, 24);
    e.base = 0xFEFF0000; e.len = 0x10000;  e.type = PULSE_E820_BAD;
    memcpy(buf + 2 * 24, &e, 24);

    pulse_memmap_t mm;
    CHECK_EQ(memmap_from_e820(&mm, buf, 3, 24), 0);
    CHECK_EQ(mm.count, 3);
    CHECK_EQ(mm.entry[0].type, PULSE_MEM_USABLE);
    CHECK_EQ(mm.entry[0].base, 0x100000);
    /* finalize() sorted by base: 0xFEFF0000 (bad) before 0xFFFC0000 */
    CHECK_EQ(mm.entry[1].type, PULSE_MEM_BAD);
    CHECK_EQ(mm.entry[1].base, 0xFEFF0000);
    CHECK_EQ(mm.entry[2].type, PULSE_MEM_RESERVED);
    CHECK_EQ(mm.entry[2].base, 0xFFFC0000);

    /* unknown type -> reserved, zero-len dropped */
    UINT8 b2[2 * 20];
    memset(b2, 0, sizeof(b2));
    memcpy(b2, (UINT64[]){0x1000}, 8);
    memcpy(b2 + 8, (UINT64[]){0x1000}, 8);
    memcpy(b2 + 16, (UINT32[]){99}, 4);
    memcpy(b2 + 20, (UINT64[]){0x8000}, 8);
    memcpy(b2 + 28, (UINT64[]){0x0}, 8);          /* zero len: dropped */
    memcpy(b2 + 36, (UINT32[]){PULSE_E820_ACPI_NVS}, 4);
    CHECK_EQ(memmap_from_e820(&mm, b2, 2, 20), 0);
    CHECK_EQ(mm.count, 1);
    CHECK_EQ(mm.entry[0].type, PULSE_MEM_RESERVED);
    CHECK_EQ(mm.entry[0].base, 0x1000);
}

/* ---------------- config v2 ---------------- */

static void test_config_entries(void)
{
    printf("config: multi-entry\n");
    static const char conf[] =
        "timeout = 3\n"
        "default = 1\n"
        "\n"
        "[entry k32]\n"
        "kernel=/boot/k32.elf\n"
        "cmdline=\"first\"\n"
        "\n"
        "[entry long name here]\n"
        "kernel=/boot/k64.elf\n";
    pulse_config_t cfg;
    UINTN line = 0;
    CHECK_EQ(config_parse(conf, sizeof(conf) - 1, &cfg, &line), 0);
    CHECK_EQ(cfg.timeout, 3);
    CHECK_EQ(cfg.default_idx, 1);
    CHECK_EQ(cfg.count, 2);
    CHECK(!strcmp(cfg.entry[0].title, "k32"));
    CHECK(!strcmp(cfg.entry[0].kernel, "/boot/k32.elf"));
    CHECK(!strcmp(cfg.entry[0].cmdline, "first"));
    CHECK(!strcmp(cfg.entry[1].title, "long name here"));
    CHECK(!strcmp(cfg.entry[1].kernel, "/boot/k64.elf"));

    /* v1 compat: no sections */
    static const char v1[] = "kernel=/only\ncmdline=x\n";
    CHECK_EQ(config_parse(v1, sizeof(v1) - 1, &cfg, NULL), 0);
    CHECK_EQ(cfg.count, 1);
    CHECK(!strcmp(cfg.entry[0].title, "entry"));

    /* trailing empty section dropped; default clamped */
    static const char trail[] =
        "[a]\nkernel=/a\n[b]\n";
    CHECK_EQ(config_parse(trail, sizeof(trail) - 1, &cfg, NULL), 0);
    CHECK_EQ(cfg.count, 1);
}

/* ---------------- menu engine ---------------- */

typedef struct {
    int keys[16];
    UINTN nkeys, pos;
    UINTN ms;
    INT32 last_attr_row[8];
    char last_line[8][96];
    INT32 draws;
} fake_io_t;

static fake_io_t *g_fio;

static void f_put(INT32 row, INT32 col, INT32 attr, const char *s)
{
    (void)col;
    if (row >= 0 && row < 8) {
        g_fio->last_attr_row[row] = attr;
        strncpy(g_fio->last_line[row], s,
                sizeof(g_fio->last_line[row]) - 1);
    }
    g_fio->draws++;
}

static INT32 f_getkey(VOID)
{
    if (g_fio->pos < g_fio->nkeys)
        return g_fio->keys[g_fio->pos++];
    return KEY_NONE;
}

static VOID f_msleep(UINT32 m) { g_fio->ms += m; }

static void test_menu(void)
{
    printf("menu: navigation + timeout\n");
    static const char *const titles[] = { "one", "two", "three" };
    fake_io_t f;
    memset(&f, 0, sizeof(f));
    g_fio = &f;

    menu_io_t io = { f_put, f_getkey, f_msleep };
    menu_desc_t d;
    memset(&d, 0, sizeof(d));
    d.titles = titles;
    d.count = 3;
    d.selected = 0;
    d.timeout_s = 5;
    d.header = "Pulse";

    /* down, down -> select "three"; then timeout expires */
    f.keys[0] = KEY_DOWN;
    f.keys[1] = KEY_DOWN;
    f.nkeys = 2;
    UINTN picked = menu_run(&io, &d);
    CHECK_EQ(picked, 2);
    CHECK(f.ms >= 5u * 1000u - 50u);            /* waited full timeout */
    CHECK(f.draws > 0);
    /* hilite landed on some row with "> three" */
    INT32 hilite_row = -1;
    for (INT32 r = 0; r < 8; r++)
        if (f.last_attr_row[r] == MENU_ATTR_HILITE)
            hilite_row = r;
    CHECK(hilite_row > 0);

    /* enter selects immediately */
    memset(&f, 0, sizeof(f));
    g_fio = &f;
    d.selected = 1;
    f.keys[0] = KEY_ENTER;
    f.nkeys = 1;
    d.timeout_s = 30;
    CHECK_EQ(menu_run(&io, &d), 1);
    CHECK(f.ms < 1000u);                        /* no timeout wait */
}

/* ---------------- fat driver over synthetic volumes ---------------- */

#define TSEC 512
typedef struct {
    UINT8 *img;
    UINT64 size;
    UINTN bps, spc, reserved, nfats;
    UINT32 fatsz, totsec, data_start, root_ents, root_lba;
    INT32 type;
    UINT32 next_free;
    UINT8 *fat;                     /* fat[0] mirror for building */
} fb_t;

static void put16(UINT8 *p, UINT16 v);
static void put32x(UINT8 *p, UINT32 v);

static void fb_init(fb_t *b, INT32 type)
{
    memset(b, 0, sizeof(*b));
    b->type = type;
    b->bps = 512;
    b->spc = 1;
    b->reserved = 1;
    b->nfats = 1;

    UINT32 minclus = type == 12 ? 10 : type == 16 ? 4085 : 65525;
    /* FAT32 needs headroom so overhead doesn't drop us below the band */
    UINT32 slack = type == 32 ? 600 : 4;
    b->root_ents = (type == 32) ? 0 : 16;           /* 1 sector */
    /* data sectors so that cluster count lands in the right band */
    UINT32 dsect = minclus + slack;
    b->fatsz = (UINT32)((minclus * (type == 12 ? 1.5 : type == 16 ? 2 : 4)
                         + b->reserved + 2)
                        / b->bps) + 1;
    if (type == 32)
        b->spc = 1;
    b->totsec = b->reserved + b->nfats * b->fatsz + 1 + dsect;
    b->size = (UINT64)b->totsec * b->bps;
    b->img = calloc(1, b->size);
    if (!b->img) {
        fprintf(stderr, "alloc fail\n");
        exit(2);
    }
    b->fat = calloc(b->fatsz * b->bps, 1);

    UINT8 *sb = b->img;
    memcpy(sb, "PULSEBOOT", 9);                     /* jump-ish */
    sb[0] = 0xEB; sb[1] = 0x3C; sb[2] = 0x90;
    put16(sb + 11, (UINT16)b->bps);
    sb[13] = (UINT8)b->spc;
    put16(sb + 14, (UINT16)b->reserved);
    sb[16] = (UINT8)b->nfats;
    put16(sb + 17, (UINT16)b->root_ents);
    put16(sb + 19, (UINT16)(b->totsec <= 0xFFFF ? b->totsec : 0));
    put32x(sb + 32, b->totsec > 0xFFFF ? b->totsec : 0);
    sb[21] = 0xF8;
    put16(sb + 22, (UINT16)(type != 32 ? b->fatsz : 0));
    put32x(sb + 36, type == 32 ? b->fatsz : 0);
    if (type == 32)
        put32x(sb + 44, 2);                         /* root cluster */

    put16(sb + 510, 0xAA55);
    b->fat[0] = 0xF8;
    b->fat[1] = 0xFF;
    if (type >= 16)
        b->fat[2] = 0xFF, b->fat[3] = 0xFF;
    /* FAT12/16: fixed root region right after the FATs.
       FAT32: no fixed root — root is cluster 2 at the data area start. */
    b->root_lba = b->reserved + b->nfats * b->fatsz;
    b->data_start = (type == 32) ? b->root_lba : b->root_lba + 1;
    b->next_free = 2;
}

static UINT32 fb_alloc_cluster(fb_t *b, const UINT8 *data, UINTN len)
{
    UINT32 c = b->next_free++;
    UINTN cap = (UINTN)b->bps * b->spc;
    UINT8 *dst = b->img + (UINT64)b->data_start * b->bps +
                 (UINT64)(c - 2) * cap;
    memcpy(dst, data, len < cap ? len : cap);
    UINT32 eoc = b->type == 12 ? 0xFFF : b->type == 16 ? 0xFFFF : 0xFFFFFFF;
    /* mark EOC */
    UINTN fo = (UINTN)c * (b->type == 12 ? 3 : b->type == 16 ? 2 : 4);
    if (b->type == 12 && (c & 1)) {
        b->fat[fo] |= eoc << 4;
        if (fo + 1 < b->fatsz * b->bps)
            b->fat[fo + 1] = (UINT8)(eoc >> 4);
    } else {
        b->fat[fo] = (UINT8)eoc;
        if (b->type != 12)
            b->fat[fo + 1] = (UINT8)(eoc >> 8);
        if (b->type == 32)
            b->fat[fo + 2] = (UINT8)(eoc >> 16), b->fat[fo + 3] = 0;
    }
    return c;
}


/* add one dirent (short + optional LFN run) into a dir cluster chain
   represented linearly here: our test dirs fit one cluster/sector */
static void fb_dirent(fb_t *b, UINT32 dir_cluster_or_root,
                      const char *name, UINT32 clus, UINT32 size,
                      UINT8 attr, int is_root_fixed)
{
    UINT8 *dir;
    if (is_root_fixed)
        dir = b->img + b->root_lba * b->bps;
    else
        dir = b->img + (UINT64)b->data_start * b->bps +
              (UINT64)(dir_cluster_or_root - 2) * b->bps;
    UINTN slot = 0;
    while (!(dir[slot * 32] == 0 || dir[slot * 32] == 0xE5))
        slot++;
    UINTN nl = strlen(name);
    char shortn[12];
    const char *dot = strrchr(name, '.');
    UINTN base_len = dot ? (UINTN)(dot - name) : nl;
    UINTN ext_len = dot ? strlen(dot + 1) : 0;
    int need_lfn = base_len > 8 || ext_len > 3 ||
                   strpbrk(name, "abcdefghij") != NULL;
    printf("  dirent[%s] slot=%u lfn=%d nrec=%d\n", name, slot,
           need_lfn, (int)((nl + 12) / 13));

    if (need_lfn) {
        /* LFN records are stored N..1; record N carries bit 0x40 */
        static const UINTN pos[] = { 1,3,5,7,9,14,16,18,20,22,24,28,30 };
        UINTN nrec = (nl + 12) / 13;
        for (UINTN r = 0; r < nrec; r++) {
            UINT8 *l = dir + (slot + r) * 32;
            memset(l, 0, 32);
            UINTN seq = nrec - r;               /* physical goes N..1 */
            l[0] = (UINT8)(seq | (r == 0 ? 0x40 : 0));
            l[11] = 0x0F;
            l[12] = 0;
            /* fragment r covers chars [r*13, r*13+13) */
            for (UINTN k = 0; k < 13; k++) {
                UINTN idx = r * 13 + k;
                UINT16 ch = idx < nl ? (UINT16)(UINT8)name[idx]
                                     : (idx == nl ? 0 : 0xFFFF);
                put16(l + pos[k], ch);
            }
        }
        slot += nrec;                            /* short follows LFNs */
        memset(shortn, ' ', 11);
        UINTN b8 = base_len < 8 ? base_len : 8;
        for (UINTN i = 0; i < b8; i++)
            shortn[i] = (char)(name[i] >= 'a' && name[i] <= 'z'
                                   ? name[i] - 32 : name[i]);
        for (UINTN i = 0; i < ext_len && i < 3; i++)
            shortn[8 + i] = (char)(dot[1 + i] >= 'a' && dot[1+i] <= 'z'
                                       ? dot[1 + i] - 32 : dot[1 + i]);
        shortn[6] = '~'; shortn[7] = '1';
    } else {
        memset(shortn, ' ', 11);
        UINTN bl = base_len < 8 ? base_len : 8;
        for (UINTN i = 0; i < bl; i++)
            shortn[i] = (char)(name[i] >= 'a' && name[i] <= 'z'
                                   ? name[i] - 32 : name[i]);
        for (UINTN i = 0; i < ext_len && i < 3; i++)
            shortn[8 + i] = (char)(dot[1 + i] >= 'a' && dot[1+i] <= 'z'
                                       ? dot[1 + i] - 32 : dot[1 + i]);
    }

    UINT8 *d = dir + slot * 32;
    memset(d, 0, 32);
    memcpy(d, shortn, 11);
    printf("    wrote short [%s] @slot=%u first=%02x\n",
           shortn, slot, d[0]);
    d[11] = attr;
    put16(d + 26, (UINT16)(clus & 0xFFFF));
    if (b->type == 32)
        put16(d + 20, (UINT16)(clus >> 16));
    put32x(d + 28, size);
}

static void fb_flush_fat(fb_t *b)
{
    memcpy(b->img + (UINT64)b->reserved * b->bps, b->fat,
           b->fatsz * b->bps);
}

static INT32 t_read_sec(VOID *ctx, UINT64 lba, VOID *buf)
{
    fb_t *b = ctx;
    if (lba >= b->totsec)
        return FS_E_IO;
    memcpy(buf, b->img + lba * b->bps, b->bps);
    return FS_OK;
}

static void test_fat(void)
{
    printf("fat: synthetic volumes\n");
    static const struct { INT32 type; } cases[] = {{12},{16},{32}};
    for (unsigned ci = 0; ci < 3; ci++) {
        fb_t b;
        fb_init(&b, cases[ci].type);

        int fixed_root = cases[ci].type != 32;

        /* /KERNEL.ELF in root (8.3) */
        UINT32 c = fb_alloc_cluster(&b,
            (const UINT8 *)"KERNEL-DATA-1234567890", 22);
        fb_dirent(&b, fixed_root ? 0 : 2, "kernel.elf", c, 22, 0,
                  fixed_root);

        /* /boot dir with long-named file inside */
        UINT32 boot_c = fb_alloc_cluster(&b, (const UINT8 *)"", 0);
        fb_dirent(&b, fixed_root ? 0 : 2, "boot", boot_c, 0,
                  T_ATTR_DIRECTORY, fixed_root);

        const char *conf = "timeout=2\ndefault=0\n";
        UINT32 cc = fb_alloc_cluster(&b, (const UINT8 *)conf,
                                     strlen(conf));
        fb_dirent(&b, boot_c, "pulse.conf", cc, (UINT32)strlen(conf),
                  0, 0);
        const char *kern = "K64PAYLOAD";
        UINT32 kc = fb_alloc_cluster(&b, (const UINT8 *)kern,
                                     strlen(kern));
        fb_dirent(&b, boot_c, "a-very-long-kernel-name.elf", kc,
                  (UINT32)strlen(kern), 0, 0);

        fb_flush_fat(&b);
        if (cases[ci].type == 32)
            printf("build32: fatsz=%u totsec=%u data_start=%u\n",
                   b.fatsz, b.totsec, b.data_start);
        {
            extern int fatdbg_dump_dir;
            if (fatdbg_dump_dir) {
                UINTN off = cases[ci].type == 32
                                ? b.data_start * b.bps
                                : b.root_lba * b.bps;
                printf("root/dir @%lx:\n", off);
                for (UINTN q = 0; q < 192; q++) {
                    printf("%02x", b.img[off + q]);
                    if ((q & 31) == 31) printf("\n");
                }
            }
        }

        blkdev_t dev = { &b, TSEC, b.totsec, t_read_sec };
        CHECK_EQ(fat_driver.probe(&dev), FS_OK);
        CHECK_EQ(fat_driver.mount(&dev, NULL, 0), FS_OK);

        vfs_node_t n;
        CHECK_EQ(fat_driver.lookup("/KERNEL.ELF", &n), FS_OK);
        CHECK_EQ(n.size, 22);

        UINT8 out[64];
        UINTN got = 0;
        CHECK_EQ(fat_driver.read(&n, 0, out, sizeof(out), &got), FS_OK);
        CHECK_EQ(got, 22);
        CHECK(memcmp(out, "KERNEL-DATA-1234567890", 22) == 0);

        CHECK_EQ(fat_driver.lookup("/boot/pulse.conf", &n), FS_OK);
        CHECK_EQ(fat_driver.read(&n, 0, out, sizeof(out), &got), FS_OK);
        CHECK_EQ(got, strlen(conf));
        CHECK(memcmp(out, conf, got) == 0);

        CHECK_EQ(fat_driver.lookup(
                     "/boot/a-very-long-kernel-name.elf", &n), FS_OK);
        CHECK_EQ(fat_driver.read(&n, 0, out, sizeof(out), &got), FS_OK);
        CHECK(memcmp(out, kern, got) == 0);

        CHECK_EQ(fat_driver.lookup("/nope.bin", &n), FS_E_NOTFOUND);
        free(b.img);
        free(b.fat);
    }

    /* probe rejects junk */
    fb_t b;
    fb_init(&b, 16);
    b.img[510] = 0;
    blkdev_t dev = { &b, TSEC, b.totsec, t_read_sec };
    CHECK_EQ(fat_driver.probe(&dev), FS_E_PROBE);
    free(b.img);
    free(b.fat);
}

int main(void)
{
    test_config_basic();
    test_config_errors();
    test_elf64();
    test_elf32();
    test_elf_rejects();
    test_memmap();
    test_memmap_e820();
    test_config_entries();
    test_menu();
    test_fat();

    if (failures) {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("all core tests passed\n");
    return 0;
}
