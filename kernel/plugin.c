#include "plugin.h"
#include "module.h"

int plugin_install(const char* name) {
    return module_install(name);
}

int plugin_uninstall(const char* name) {
    return module_uninstall(name);
}

u32 plugin_count(void) {
    return module_count();
}
