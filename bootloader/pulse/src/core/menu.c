#include "menu.h"
#include "lib.h"

#define ROWS_BODY_MAX 10

static void draw(const menu_io_t *io, const menu_desc_t *d,
                 INT32 seconds_left)
{
    char line[96];
    INT32 row = 1;

    io->put(row++, 2, MENU_ATTR_HEADER, d->header ? d->header : "Boot");

    for (UINTN i = 0; i < d->count && i < ROWS_BODY_MAX; i++, row++) {
        line[0] = d->selected == i ? '>' : ' ';
        line[1] = ' ';
        UINTN tl = strlen(d->titles[i]);
        if (tl > 70)
            tl = 70;
        memcpy(line + 2, d->titles[i], tl);
        line[2 + tl] = 0;
        io->put(row, 2,
                d->selected == i ? MENU_ATTR_HILITE : MENU_ATTR_NORMAL,
                line);
    }

    if (d->timeout_s)
        row++;                       /* blank line before footer */
    if (d->timeout_s && seconds_left > 0) {
        char num[4];
            UINT32 v = (UINT32)seconds_left;
            num[3] = 0;
            num[2] = (char)('0' + v % 10);
            v /= 10;
            num[1] = v ? (char)('0' + v % 10) : ' ';
            v /= 10;
            num[0] = v ? (char)('0' + v % 10) : ' ';
        memcpy(line, "Booting selection in ", 21);
        memcpy(line + 21, num, 4);
        memcpy(line + 25, " s  (arrows + Enter)", 21);
        line[46] = 0;
        io->put(row, 2, MENU_ATTR_FOOTER, line);
    } else if (d->timeout_s) {
        memcpy(line, "Booting...", 10);
        line[10] = 0;
        io->put(row, 2, MENU_ATTR_FOOTER, line);
    }
}

UINTN menu_run(const menu_io_t *io, menu_desc_t *d)
{
    if (!io || !io->put || !io->getkey || !io->msleep || !d ||
        d->count == 0)
        return 0;
    if (d->selected >= d->count)
        d->selected = 0;

    UINT32 elapsed_ms = 0;
    INT32 shown_sec = -1;

    for (;;) {
        INT32 total = (INT32)(d->timeout_s * 1000u);
        INT32 left = d->timeout_s ? total - (INT32)elapsed_ms : -1;
        INT32 sec = d->timeout_s
                        ? (left + 999) / 1000
                        : 0;
        if (sec != shown_sec) {
            draw(io, d, sec);
            shown_sec = sec;
        }

        INT32 k = io->getkey();
        if (k == KEY_UP) {
            d->selected = d->selected == 0
                              ? d->count - 1
                              : d->selected - 1;
            shown_sec = -1;             /* force redraw */
        } else if (k == KEY_DOWN) {
            d->selected = (d->selected + 1) % d->count;
            shown_sec = -1;
        } else if (k == KEY_ENTER) {
            return d->selected;
        }

        if (d->timeout_s) {
            if (left <= 0)
                return d->selected;
            io->msleep(50);
            elapsed_ms += 50;
        }
    }
}
