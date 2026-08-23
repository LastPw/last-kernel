#ifndef LASTOS_AXIOM_WEB_H
#define LASTOS_AXIOM_WEB_H

#include "types.h"
#include "user_apps.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Axiom Web Engine (AxiomWeb / LibAxiomDOM)
 * Next-Generation Sovereign Web Engine for LastOS
 * Features:
 *   - Sovereign HTML5 Tokenizer & DOM Tree Engine (Hierarchical Nodes)
 *   - Axiom CSS3 Cascading Style & Box Model Layout Engine
 *   - AxiomScript / DOM Scripting Runtime (document.getElementById, events)
 *   - Native BearSSL TLS 1.3 & TCP/IP Socket Stream Fetcher
 *   - Developer Tools (F12 Live DOM Inspector & Console Log)
 */

#define WEB_MAX_NODES     256
#define WEB_MAX_BOXES     256
#define WEB_MAX_TABS      6
#define WEB_MAX_URL       256
#define WEB_MAX_SELECTORS 64
#define WEB_MAX_SCRIPTS   32
#define WEB_CONSOLE_LINES 16

/* DOM Node Types */
typedef enum {
    DOM_NODE_DOCUMENT = 0,
    DOM_NODE_ELEMENT,
    DOM_NODE_TEXT,
    DOM_NODE_COMMENT
} dom_node_type_t;

/* HTML Element Tag Types */
typedef enum {
    TAG_UNKNOWN = 0,
    TAG_HTML,
    TAG_HEAD,
    TAG_BODY,
    TAG_TITLE,
    TAG_H1,
    TAG_H2,
    TAG_H3,
    TAG_H4,
    TAG_H5,
    TAG_H6,
    TAG_P,
    TAG_A,
    TAG_DIV,
    TAG_SPAN,
    TAG_B,
    TAG_STRONG,
    TAG_I,
    TAG_EM,
    TAG_U,
    TAG_CODE,
    TAG_PRE,
    TAG_BLOCKQUOTE,
    TAG_UL,
    TAG_OL,
    TAG_LI,
    TAG_TABLE,
    TAG_THEAD,
    TAG_TBODY,
    TAG_TR,
    TAG_TH,
    TAG_TD,
    TAG_HR,
    TAG_BR,
    TAG_IMG,
    TAG_FORM,
    TAG_INPUT,
    TAG_BUTTON,
    TAG_STYLE,
    TAG_SCRIPT,
    TAG_NAV,
    TAG_HEADER,
    TAG_FOOTER,
    TAG_SECTION,
    TAG_ARTICLE,
    TAG_ASIDE
} html_tag_t;

/* CSS Display Types */
typedef enum {
    CSS_DISPLAY_BLOCK = 0,
    CSS_DISPLAY_INLINE,
    CSS_DISPLAY_INLINE_BLOCK,
    CSS_DISPLAY_FLEX,
    CSS_DISPLAY_NONE
} css_display_t;

/* Computed CSS Style for a DOM Node */
typedef struct {
    u32 color;
    u32 bg_color;
    u32 border_color;
    u8  border_width;
    u8  border_radius;
    u16 font_size;       /* in pixels: 12, 14, 16, 18, 22, 28 */
    u8  font_bold;
    u8  font_italic;
    u8  text_align;      /* 0: left, 1: center, 2: right */
    u8  text_underline;
    css_display_t display;
    u8  margin_top, margin_bottom, margin_left, margin_right;
    u8  padding_top, padding_bottom, padding_left, padding_right;
    u32 width, height;   /* 0 = auto */
    u8  has_bg;
    u8  has_border;
} css_style_t;

/* DOM Tree Node */
typedef struct dom_node {
    dom_node_type_t type;
    html_tag_t      tag;
    char            tag_name[20];
    char            id[32];
    char            class_name[48];
    char            href[128];
    char            src[128];
    char            alt[64];
    char            value[64];
    char            placeholder[64];
    char            text_content[160];
    css_style_t     style;
    
    struct dom_node* parent;
    struct dom_node* first_child;
    struct dom_node* next_sibling;
    
    /* Layout coordinates */
    u32 x, y, w, h;
} dom_node_t;

/* Render Box calculated by the Box Model layout pass */
typedef struct {
    dom_node_t* node;
    u32 x, y, w, h;
    u32 text_color;
    u32 bg_color;
    u32 border_color;
    u8  border_radius;
    u8  border_width;
    u8  font_id;
    char text[160];
    char link_href[128];
    u8  is_interactive;  /* clickable link, button, or input */
    u8  is_input;
} layout_box_t;

/* Browser Tab Structure */
typedef struct {
    char title[36];
    char url[WEB_MAX_URL];
    char raw_html[8192];
    u32  html_len;
    i32  scroll_y;
    i32  max_scroll_y;
    u8   is_loading;
    u8   is_secure;      /* TLS 1.3 */
    u8   active;
} web_tab_t;

#include "axiom_js.h"

/* Browser State */
typedef struct {
    web_tab_t tabs[WEB_MAX_TABS];
    u32 active_tab;
    u32 tabs_count;
    
    char omnibox_input[WEB_MAX_URL];
    u32  omnibox_len;
    u8   omnibox_editing;
    
    char status_text[96];
    
    /* DevTools Panel & Interactive REPL */
    u8   devtools_open;
    char console_log[WEB_CONSOLE_LINES][80];
    u32  console_count;
    char devtools_input[128];
    u32  devtools_input_len;
    
    /* JavaScript Runtime Engine */
    js_vm_t js_vm;
    
    /* History Navigation */
    char history[16][WEB_MAX_URL];
    u32  hist_count;
    u32  hist_pos;
    
    /* Engine Nodes & Boxes */
    dom_node_t   nodes[WEB_MAX_NODES];
    u32          node_count;
    dom_node_t*  root_node;
    
    layout_box_t boxes[WEB_MAX_BOXES];
    u32          box_count;
} axiom_browser_state_t;

/* Public API */
void axiom_web_init(void);
void axiom_web_render(app_instance_t* app);
void axiom_web_handle_mouse(app_instance_t* app, i32 mx, i32 my, u8 btns);
void axiom_web_handle_key(app_instance_t* app, int key);
void axiom_web_navigate(const char* url);
void axiom_web_load_html(const char* html, u32 len, const char* base_url);
void axiom_web_log(const char* msg);
dom_node_t* axiom_web_find_node_by_id(const char* id);
void axiom_web_relayout(void);

#ifdef __cplusplus
}
#endif

#endif /* LASTOS_AXIOM_WEB_H */
