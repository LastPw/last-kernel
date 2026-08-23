#ifndef MEDIA_PLAYER_APP_H
#define MEDIA_PLAYER_APP_H

#include "types.h"

typedef struct {
    u8 open;
    u8 anim;
    u8 state; /* 0 stopped, 1 playing, 2 paused */
    u8 track;
    u8 volume;
    u8 repeat;
} media_player_app_t;

void media_player_app_init(media_player_app_t* app);
void media_player_app_open(media_player_app_t* app);
void media_player_app_handle_key(media_player_app_t* app, int ch);

#endif

