/* BIOS boot menu: VGA text (0xB8000) + raw PS/2 port 0x60/0x64.
   Implements the same menu_io_t contract as the UEFI console backend. */
#ifndef PULSE_BIOS_UI_H
#define PULSE_BIOS_UI_H

#include "menu.h"
#include "pulsebase.h"  /* via -Iinc/core */

/* titles + availability come from stage2's loaded buffers */
UINTN bios_menu(UINTN count, const char *const *titles,
                UINT32 timeout_s, UINTN preselect);

#endif
