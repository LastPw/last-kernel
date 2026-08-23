#ifndef CODE_EDITOR_APP_H
#define CODE_EDITOR_APP_H

#include "types.h"

#define CODE_EDITOR_MAX 768

typedef struct {
    u8 open;
    u8 anim;
    u8 dirty;
    u8 language_id; /* 0 plain,1 c,2 cpp,3 py,4 js,5 ts,6 rs,7 go,8 java,9 html,10 json,11 sh,12 md,13 sql,14 yaml */
    u16 len;
    u8 suggest_visible;
    char name[24];
    char buf[CODE_EDITOR_MAX];
    char suggestion[32];
} code_editor_app_t;

void code_editor_app_init(code_editor_app_t* app);
void code_editor_app_open(code_editor_app_t* app);
void code_editor_app_handle_key(code_editor_app_t* app, int ch);
void code_editor_app_save(code_editor_app_t* app);

#endif
