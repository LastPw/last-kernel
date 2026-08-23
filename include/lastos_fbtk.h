#ifndef LASTOS_FBTK_H
#define LASTOS_FBTK_H

#include "types.h"
#include "font_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FB_WIDGET_TYPE_ROOT = 0,
    FB_WIDGET_TYPE_WINDOW,
    FB_WIDGET_TYPE_BITMAP,
    FB_WIDGET_TYPE_FILL,
    FB_WIDGET_TYPE_TEXT,
    FB_WIDGET_TYPE_HSCROLL,
    FB_WIDGET_TYPE_VSCROLL,
    FB_WIDGET_TYPE_USER
} fbtk_widgettype_t;

typedef enum {
    FB_EVENT_KEY,
    FB_EVENT_MOUSE_CLICK,
    FB_EVENT_MOUSE_MOVE,
    FB_EVENT_REDRAW
} fbtk_eventtype_t;

typedef struct fbtk_widget_s fbtk_widget_t;

typedef int (*fbtk_callback_t)(fbtk_widget_t* widget, fbtk_eventtype_t event, void* event_data);

struct fbtk_widget_s {
    fbtk_widgettype_t type;
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    u8 mapped;
    u32 bg_color;
    u32 fg_color;
    char text[128];

    /* Scroll parameters */
    i32 scroll_min;
    i32 scroll_max;
    i32 scroll_pos;
    i32 scroll_thumb_size;

    fbtk_callback_t callback;
    void* user_data;

    /* Hierarchy */
    struct fbtk_widget_s* parent;
    struct fbtk_widget_s* first_child;
    struct fbtk_widget_s* last_child;
    struct fbtk_widget_s* next;
    struct fbtk_widget_s* prev;
};

/* FBTK Public API */
fbtk_widget_t* fbtk_init(u32 width, u32 height);
void fbtk_destroy(fbtk_widget_t* root);

fbtk_widget_t* fbtk_create_window(fbtk_widget_t* parent, i32 x, i32 y, u32 width, u32 height, u32 bg_color);
fbtk_widget_t* fbtk_create_button(fbtk_widget_t* parent, i32 x, i32 y, u32 width, u32 height, const char* text, fbtk_callback_t cb, void* user_data);
fbtk_widget_t* fbtk_create_fill(fbtk_widget_t* parent, i32 x, i32 y, u32 width, u32 height, u32 color);
fbtk_widget_t* fbtk_create_text(fbtk_widget_t* parent, i32 x, i32 y, u32 width, u32 height, const char* text, u32 color);
fbtk_widget_t* fbtk_create_vscroll(fbtk_widget_t* parent, i32 x, i32 y, u32 width, u32 height, fbtk_callback_t cb, void* user_data);

void fbtk_set_scroll(fbtk_widget_t* widget, i32 pos, i32 max, i32 thumb);
void fbtk_render_tree(fbtk_widget_t* widget, i32 abs_x, i32 abs_y);
int fbtk_dispatch_mouse(fbtk_widget_t* widget, i32 mx, i32 my, u8 btns);
int fbtk_dispatch_key(fbtk_widget_t* widget, int key);

#ifdef __cplusplus
}
#endif

#endif /* LASTOS_FBTK_H */
