#include "uefi.h"
#include "fs.h"
#include "config.h"
#include "menu.h"
#include "elf.h"
#include "memmap.h"
#include "mb2.h"
#include "lib.h"

extern void pulse_serial_puts(const char *s);
extern void pulse_jump_mb2(UINTN entry, UINTN mbi) __attribute__((ms_abi));

static EFI_SYSTEM_TABLE *g_st;
static EFI_BOOT_SERVICES *g_bs;

static void print(const char *s)
{
    CHAR16 buf[128];
    g_st->ConOut->OutputString(g_st->ConOut, widen16(s, buf, 128));
}

static void print_hex(UINT64 v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++)
        buf[2 + i] = hex[(v >> (60 - 4 * i)) & 0xF];
    buf[18] = 0;
    CHAR16 w[19];
    g_st->ConOut->OutputString(g_st->ConOut, widen16(buf, w, 19));
}

/* ---- ELF destination allocator: fixed-address page reservation ---- */
static VOID *elf_alloc(VOID *ctx, UINT64 phys, UINT64 len)
{
    (VOID)ctx;
    UINT64 start = phys & ~(EFI_PAGE_SIZE - 1);
    UINT64 end = (phys + len + EFI_PAGE_SIZE - 1) & ~(EFI_PAGE_SIZE - 1);
    UINTN pages = (UINTN)((end - start) >> 12);
    (void)g_bs->AllocatePages(AllocateAddress, EfiLoaderData, pages, &start);
    return (VOID *)(UINTN)phys;
}

/* ---- boot menu over ConOut + ConIn ---- */
static VOID m_put(INT32 row, INT32 col, INT32 attr, const char *txt)
{
    static const UINTN attr_map[] = { 0x0F, 0x0E, 0x07, 0x70, 0x0B };
    CHAR16 w[80];
    UINTN n = 0;
    while (txt[n] && n < 79) {
        w[n] = (CHAR16)(UINT8)txt[n];
        n++;
    }
    w[n] = 0;
    g_st->ConOut->SetAttribute(g_st->ConOut, attr_map[attr]);
    g_st->ConOut->SetPosition(g_st->ConOut, (UINTN)col, (UINTN)row);
    g_st->ConOut->OutputString(g_st->ConOut, w);
}

static INT32 m_getkey(VOID)
{
    EFI_INPUT_KEY k;
    if (IS_ERROR(g_st->ConIn->ReadKeyStroke(g_st->ConIn, &k)))
        return KEY_NONE;
    if (k.ScanCode == SCAN_UP)
        return KEY_UP;
    if (k.ScanCode == SCAN_DOWN)
        return KEY_DOWN;
    if (k.UnicodeChar == 0x0D || k.ScanCode == 0x0D)
        return KEY_ENTER;
    return KEY_NONE;
}

static VOID m_msleep(UINT32 ms)
{
    /* Firmware Stall() hung under OVMF here; a calibrated busy-wait is
       all a menu countdown needs anyway (~1 us per port-0x80 read). */
    for (UINT32 i = 0; i < ms * 1000u; i++)
        __asm__ volatile ("inb $0x80, %%al" ::: "al");
}

static UINTN run_menu(const pulse_config_t *cfg)
{
    const char *titles[PULSE_MAX_ENTRIES];
    for (UINTN i = 0; i < cfg->count; i++)
        titles[i] = cfg->entry[i].title;

    menu_io_t io = { m_put, m_getkey, m_msleep };
    menu_desc_t d;
    d.titles = titles;
    d.count = cfg->count;
    d.selected = cfg->default_idx;
    d.timeout_s = cfg->timeout ? cfg->timeout : 5;
    d.header = "== Pulse Loader ==";

    g_st->ConOut->ClearScreen(g_st->ConOut);
    UINTN idx = menu_run(&io, &d);
    g_st->ConOut->ClearScreen(g_st->ConOut);
    return idx < cfg->count ? idx : 0;
}

static EFI_STATUS get_memmap(pulse_memmap_t *norm, UINTN *map_key)
{
    /* Buffer comes from the firmware heap, not our image: keeping it in
       .bss would embed 16 KiB of zeros in the EFI binary. It stays
       allocated until ExitBootServices so the returned key stays valid. */
    static VOID *mapbuf;
    if (!mapbuf && IS_ERROR(g_bs->AllocatePool(EfiLoaderData, 16384,
                                               &mapbuf)))
        return EFIERR(EFI_NOT_FOUND);

    UINTN size = 16384, dsize = 0, key = 0;
    UINT32 dver = 0;
    EFI_STATUS st = g_bs->GetMemoryMap(&size, mapbuf, &key, &dsize, &dver);
    if (IS_ERROR(st))
        return st;
    memmap_from_uefi(norm, mapbuf, size,
                     dsize ? dsize : sizeof(EFI_MEMORY_DESCRIPTOR));
    *map_key = key;
    return EFI_SUCCESS;
}

EFIAPI
EFI_STATUS efi_main(VOID *ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    g_st = SystemTable;
    g_bs = SystemTable->BootServices;

    pulse_serial_puts("Pulse Loader v0.2\r\n");
    print("Pulse Loader v0.2\r\n");

    /* ---- filesystem ---- */
    EFI_FILE_PROTOCOL *root = NULL;
    fs_init(g_bs);
    if (IS_ERROR(fs_locate_root(&root))) {
        print("no filesystem\r\n");
        goto hang;
    }

    /* ---- config ---- */
    VOID *conf_raw = NULL;
    UINTN conf_len = 0;
    /* no wide-string literals: freestanding + 2-byte CHAR16 */
    static const CHAR16 conf_boot[] = {
        '\\', 'b', 'o', 'o', 't', '\\', 'p', 'u', 'l', 's', 'e',
        '.', 'c', 'o', 'n', 'f', 0
    };
    static const CHAR16 conf_root[] = {
        'p', 'u', 'l', 's', 'e', '.', 'c', 'o', 'n', 'f', 0
    };
    if (!IS_ERROR(fs_read(root, conf_boot, &conf_raw, &conf_len)) ||
        !IS_ERROR(fs_read(root, conf_root, &conf_raw, &conf_len))) {
        /* got one */
    }
    pulse_config_t cfg;
    UINTN err_line = 0;
    if (!conf_raw || config_parse(conf_raw, conf_len, &cfg, &err_line)) {
        print("bad or missing pulse.conf\r\n");
        goto hang;
    }
    UINTN pick = run_menu(&cfg);
    print("booting: ");
    print(cfg.entry[pick].title);
    print("\r\n");

    /* ---- kernel image bytes ---- */
    /* UEFI device paths use backslashes; configs may use either */
    char kpath_ascii[256];
    UINTN i = 0;
    for (; i < sizeof(kpath_ascii) - 1; i++) {
        char c = cfg.entry[pick].kernel[i];
        if (!c)
            break;
        kpath_ascii[i] = (c == '/') ? '\\' : c;
    }
    kpath_ascii[i] = 0;
    CHAR16 kpath[256];
    widen16(kpath_ascii, kpath, 256);
    VOID *img = NULL;
    UINTN img_len = 0;
    EFI_STATUS fs_st = fs_read(root, kpath, &img, &img_len);
    if (IS_ERROR(fs_st)) {
        print("kernel open failed: st=");
        print_hex((UINT64)fs_st);
        print(" path=");
        g_st->ConOut->OutputString(g_st->ConOut, kpath);
        print("\r\n");
        goto hang;
    }

    /* ---- load segments at their physical addresses ---- */
    elf_callbacks_t cb = { elf_alloc };
    UINT64 entry = 0, top = 0;
    INT32 rc = elf_load(img, img_len, &cb, NULL, &entry, &top);
    g_bs->FreePool(img);                /* raw bytes not needed anymore */
    if (rc != ELF_OK) {
        print("elf load error\r\n");
        goto hang;
    }
    print("loaded. entry=");
    print_hex(entry);
    print(" top=");
    print_hex(top);
    print("\r\n");

    /* ---- MBI lives in dedicated pages that survive ExitBootServices ----
       Build it directly in place; no scratch copy. All allocations must
       happen before the final GetMemoryMap below. */
    EFI_PHYSICAL_ADDRESS mbi_pa = 0;    /* any free pages above firmware */
    if (IS_ERROR(g_bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 8,
                                     &mbi_pa))) {
        print("MBI alloc failed\r\n");
        goto hang;
    }
    mb2_builder_t mb;
    mb2_init(&mb, (VOID *)(UINTN)mbi_pa, 8 * EFI_PAGE_SIZE);
    mb2_tag_string(&mb, 1, cfg.entry[pick].cmdline);
    mb2_tag_string(&mb, 2, "PulseLoader");
    pulse_memmap_t mm;
    UINTN key = 0;
    if (IS_ERROR(get_memmap(&mm, &key))) {
        print("memmap failed\r\n");
        goto hang;
    }
    mb2_tag_memmap(&mb, &mm);
    if (!mb2_finish(&mb)) {
        print("MBI overflow\r\n");
        goto hang;
    }
    print("MBI at ");
    print_hex(mbi_pa);
    print("\r\nbye firmware...\r\n");

    /* fresh map key: anything above invalidated earlier ones */
    if (IS_ERROR(get_memmap(&mm, &key))) {
        print("memmap(2) failed\r\n");
        goto hang;
    }
    g_bs->ExitBootServices(ImageHandle, key);

    pulse_jump_mb2((UINTN)entry, (UINTN)mbi_pa);

hang:
    print("HALTED\r\n");
    for (;;)
        __asm__ volatile ("hlt");
}
