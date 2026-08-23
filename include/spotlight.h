#ifndef LASTOS_SPOTLIGHT_H
#define LASTOS_SPOTLIGHT_H

#include "types.h"
#include "user_apps.h"

#define SPOTLIGHT_MAX_QUERY 64u
#define SPOTLIGHT_MAX_RESULTS 6u

typedef struct {
    const char* title;
    const char* subtitle;
    const char* badge;
    u32 icon_color;
    app_id_t app_id;
    u8 is_system_action;
    u32 action_id;
} spotlight_result_t;

typedef struct {
    u8 active;
    char query[SPOTLIGHT_MAX_QUERY];
    u32 query_len;
    u32 selected_idx;
    u32 result_count;
    spotlight_result_t results[SPOTLIGHT_MAX_RESULTS];
    char calc_result[32];
    u8 has_calc_result;
} spotlight_state_t;

void spotlight_init(void);
void spotlight_toggle(void);
void spotlight_open(void);
void spotlight_close(void);
spotlight_state_t* spotlight_get_state(void);
void spotlight_handle_key(int key);
void spotlight_render(void);
int  spotlight_handle_click(i32 mx, i32 my);

#endif /* LASTOS_SPOTLIGHT_H */
