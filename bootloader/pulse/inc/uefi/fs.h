#ifndef PULSE_FS_H
#define PULSE_FS_H

#include "uefi.h"

void fs_init(EFI_BOOT_SERVICES *bs);
EFI_STATUS fs_locate_root(EFI_FILE_PROTOCOL **root);
EFI_STATUS fs_read(EFI_FILE_PROTOCOL *volume, const CHAR16 *path,
                   VOID **out, UINTN *out_len);
CHAR16 *widen16(const char *s, CHAR16 *out, UINTN cap);

#endif
