/* Shared boot-menu engine. Pure logic + drawing through a tiny I/O
 * contract, so UEFI (ConOut) and BIOS (VGA text) reuse it unchanged
 * and the host tests can drive it with fake input. */
#ifndef PULSE_MENU_H
#define PULSE_MENU_H

#include "pulsebase.h"

enum {
    KEY_NONE = -1,
    KEY_UP,
    KEY_DOWN,
    KEY_ENTER,
};

/* logical attributes; each backend maps to its own colors */
enum {
    MENU_ATTR_SCREEN = 0,      /* page background */
    MENU_ATTR_HEADER,          /* title/banner text */
    MENU_ATTR_NORMAL,          /* unselected entry */
    MENU_ATTR_HILITE,          /* selected entry */
    MENU_ATTR_FOOTER,          /* countdown/help line */
};

typedef struct {
    /* write one ASCII string at row,col with a logical attribute;
       strings are short (< 80 chars), no control chars inside */
    VOID (*put)(INT32 row, INT32 col, INT32 attr, const char *s);
    INT32 (*getkey)(VOID);                 /* KEY_* above */
    VOID (*msleep)(UINT32);                /* best-effort delay */
} menu_io_t;

typedef struct {
    const char *const *titles;
    UINTN count;
    UINTN selected;                        /* in/out: preselect */
    UINT32 timeout_s;                      /* 0 = wait forever */
    const char *header;                    /* e.g. "Pulse Loader" */
} menu_desc_t;

/* Runs interactively. Returns index of chosen entry. Draws on every
 * state change plus once per second while counting down. */
UINTN menu_run(const menu_io_t *io, menu_desc_t *d);

#endif
