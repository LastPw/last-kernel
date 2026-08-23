#ifndef USER_TOOLCHAINS_H
#define USER_TOOLCHAINS_H

#include "types.h"

void toolchains_init(void);

int cmd_gcc_exec(int argc, const char* const* argv, char* out_buf, u32 max_buf);
int cmd_nasm_exec(int argc, const char* const* argv, char* out_buf, u32 max_buf);
int cmd_sqlite3_exec(int argc, const char* const* argv, char* out_buf, u32 max_buf);
int cmd_php_exec(int argc, const char* const* argv, char* out_buf, u32 max_buf);

#endif /* USER_TOOLCHAINS_H */
