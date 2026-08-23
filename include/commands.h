#ifndef COMMANDS_H
#define COMMANDS_H

#include "types.h"

void commands_execute(const char* line);
int commands_execute_status(const char* line);
int commands_execute_capture(const char* line, char* output, u32 output_size);
void commands_list(void);
const char* commands_suggest(const char* input);
const char* commands_cwd(void);
void commands_history_init(void);
void commands_env_init(void);
int commands_env_set(const char* name, const char* value);
int commands_env_expand(const char* input, char* output, u32 output_size);

#endif
