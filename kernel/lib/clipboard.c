#include "clipboard.h"
#include "string.h"

static char g_clipboard[CLIPBOARD_MAX + 1];

void clipboard_init(void) {
    g_clipboard[0] = '\0';
}

void clipboard_set(const char* text) {
    if (!text) {
        g_clipboard[0] = '\0';
        return;
    }

    str_ncpy(g_clipboard, text, sizeof(g_clipboard));
}

void clipboard_get(char* out, u32 out_size) {
    if (!out || out_size == 0) {
        return;
    }

    str_ncpy(out, g_clipboard, out_size);
}
