#ifndef CMDLINE_H
#define CMDLINE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CMDLINE_LEN   512u
#define MAX_CMDLINE_ARGS  32u

typedef struct {
    char key[32];
    char val[64];
    u8   has_val;
} cmdline_arg_t;

void        cmdline_init(const char* raw_str);
int         cmdline_has_flag(const char* key);
const char* cmdline_get_val(const char* key);
i32         cmdline_get_int(const char* key, i32 default_val);
const char* cmdline_get_raw(void);
u32         cmdline_get_arg_count(void);
void        cmdline_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* CMDLINE_H */
