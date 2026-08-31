#ifndef PKG_MANAGER_H
#define PKG_MANAGER_H

#include "types.h"

typedef struct {
    char name[32];
    char version[16];
    char category[24];
    char description[96];
    u32 size_kb;
    u8 installed;
} lastos_package_t;

typedef struct {
    u32 total_packages;
    u32 installed_count;
    lastos_package_t packages[16];
} pkg_repo_t;

int pkg_manager_init(void);
int pkg_manager_list(lastos_package_t* out_pkgs, u32 max_pkgs);
int pkg_manager_install(const char* pkg_name);
int pkg_manager_remove(const char* pkg_name);
int pkg_manager_exec(const char* pkg_name);

#endif
