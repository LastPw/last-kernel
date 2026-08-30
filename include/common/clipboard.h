#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include "types.h"

#define CLIPBOARD_MAX 120

void clipboard_init(void);
void clipboard_set(const char* text);
void clipboard_get(char* out, u32 out_size);

#endif
