#ifndef LASTGUI_GLYPH_H
#define LASTGUI_GLYPH_H

#include "types.h"

/* Shared 16x16 anti-aliased glyph metric for the UI and mono faces.
   Each texel is 0..255 ink coverage; cov[row][col] with row 0 at the top and
   col 0 at the left.  Owned by this header so both faces agree on one type. */
#define LG_FONT_WIDTH    16
#define LG_FONT_HEIGHT   16
#define LG_FONT_ADVANCE  16
typedef struct { u8 cov[LG_FONT_HEIGHT][LG_FONT_WIDTH]; } lg_glyph16_t;

#endif