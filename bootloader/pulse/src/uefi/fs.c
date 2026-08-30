/* UEFI-side helpers: filesystem access and string widening. */
#include "uefi.h"
#include "fs.h"
#include "lib.h"


static EFI_BOOT_SERVICES *g_bs;

void fs_init(EFI_BOOT_SERVICES *bs)
{
    g_bs = bs;
}

EFI_STATUS fs_locate_root(EFI_FILE_PROTOCOL **root)
{
    static EFI_GUID sfs_guid = {
        0x964e5b22, 0x6459, 0x11d2,
        { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
    };
    VOID *iface = NULL;
    EFI_STATUS st = g_bs->LocateProtocol(&sfs_guid, NULL, &iface);
    if (IS_ERROR(st) || !iface)
        return IS_ERROR(st) ? st : EFIERR(EFI_NOT_FOUND);
    return ((EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *)iface)->OpenVolume(
        iface, root);
}

CHAR16 *widen16(const char *s, CHAR16 *out, UINTN cap)
{
    UINTN i = 0;
    while (s[i] && i < cap - 1) {
        out[i] = (CHAR16)(UINT8)s[i];
        i++;
    }
    out[i] = 0;
    return out;
}

EFI_STATUS fs_read(EFI_FILE_PROTOCOL *volume, const CHAR16 *path,
                   VOID **out, UINTN *out_len)
{
    EFI_FILE_PROTOCOL *f = NULL;
    EFI_STATUS st = volume->Open(volume, &f, (CHAR16 *)path,
                                 EFI_FILE_MODE_READ, 0);
    if (IS_ERROR(st))
        return st;

    /* No size query: some IA32 firmware builds #UD on the second
       GetInfo/GetPosition of a session. Files are read with a capped
       buffer instead; Read() reports how much actually fit. */
    enum { PULSE_FILE_CAP = 4 * 1024 * 1024 };
    VOID *buf = NULL;
    st = g_bs->AllocatePool(EfiLoaderData, PULSE_FILE_CAP, &buf);
    if (IS_ERROR(st)) {
        EFI_PHYSICAL_ADDRESS pbuf = 0;
        UINTN pages = (PULSE_FILE_CAP + 4095) / 4096;
        st = g_bs->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &pbuf);
        if (IS_ERROR(st))
            goto fail;
        buf = (VOID *)(UINTN)pbuf;
    }

    UINTN got = PULSE_FILE_CAP;
    st = f->Read(f, &got, buf);
    if (IS_ERROR(st)) {
        g_bs->FreePool(buf);
        goto fail;
    }
    ((UINT8 *)buf)[got] = 0;            /* NUL-pad: configs like it */

    *out = buf;
    *out_len = got;
fail:
    f->Close(f);
    return st;
}
