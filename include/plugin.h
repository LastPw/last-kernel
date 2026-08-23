#ifndef PLUGIN_H
#define PLUGIN_H

#include "types.h"

int plugin_install(const char* name);
int plugin_uninstall(const char* name);
u32 plugin_count(void);

#endif
