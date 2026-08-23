#ifndef PERSIST_H
#define PERSIST_H

#include "types.h"

#define PERSIST_MAX_DATA 55

void persist_init(void);
void persist_save_data(const char* text);
void persist_load_data(char* out, u32 out_size);
u32 persist_boot_count(void);
int persist_auth_exists(void);
void persist_set_auth(const char* user, const char* pass);
int persist_get_auth(char* user, u32 user_size, char* pass, u32 pass_size);
int persist_auth_verify(const char* user, const char* pass);
void persist_set_hostname(const char* hostname);
int persist_get_hostname(char* out, u32 out_size);
void persist_set_ui_prefs(
    u8 theme, u8 bg_preset, u8 animations, u8 high_contrast,
    u8 prompt_color, u8 input_color, u8 ok_color, u8 err_color, u8 text_color
);
int persist_get_ui_prefs(
    u8* theme, u8* bg_preset, u8* animations, u8* high_contrast,
    u8* prompt_color, u8* input_color, u8* ok_color, u8* err_color, u8* text_color
);
int persist_install_locked(void);
int persist_lock_installation(void);

void persist_load_disk(void);

#endif
