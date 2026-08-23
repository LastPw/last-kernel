#ifndef LASTOS_AXIOM_LAYOUT_H
#define LASTOS_AXIOM_LAYOUT_H

#include "types.h"
#include "axiom_web.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AxiomLayout Multi-Pass Layout & Word-Wrapping Engine
 * Computes exact text wrapping, card containers, and table grids
 */

typedef struct {
    char lines[8][128];
    u32  line_count;
    u32  total_height;
} layout_wrapped_text_t;

void layout_wrap_text(const char* text, u8 font_id, u32 max_width, layout_wrapped_text_t* out_wrap);
void layout_build_page(dom_node_t* root, u32 vx, u32 vy, u32 vw, u32 vh);

#ifdef __cplusplus
}
#endif

#endif /* LASTOS_AXIOM_LAYOUT_H */
