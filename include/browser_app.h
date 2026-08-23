#ifndef BROWSER_APP_H
#define BROWSER_APP_H

#include "types.h"

typedef struct {
    u8 open;
    u8 anim;
    u8 status; /* 0 home, 1 loading, 2 loaded */
    u8 sandbox_on;
    u8 tab_count;
    u8 incognito;
    u8 url_len;
    char url[64];
    char search_engine[24];
    char last_host[32];
    u8 looks_like_domain;
    u8 search_mode;
    u8 https_mode;
    char history[6][64];
    u8 history_count;
    u8 history_cursor;
} browser_app_t;

void browser_app_init(browser_app_t* app);
void browser_app_open(browser_app_t* app);
void browser_app_handle_key(browser_app_t* app, int ch, u8 net_connected);

#endif
