#ifndef NUN_PKG_H
#define NUN_PKG_H

#include "types.h"

typedef struct {
    char name[32];
    char version[16];
    char category[24];
    char description[96];
    char author[32];
    u32  size_kb;
    u8   installed;
    u8   is_system_core;
    char sha256_prefix[16];
} nun_package_t;

#define NUN_MAX_REPO_PACKAGES 32u

typedef struct {
    u32 total_packages;
    u32 installed_count;
    nun_package_t packages[NUN_MAX_REPO_PACKAGES];
} nun_repo_t;

void nun_init(void);
int  nun_install(const char* pkg_name);
int  nun_remove(const char* pkg_name);
int  nun_search(const char* query, nun_package_t* out_results, u32 max_results);
int  nun_list_installed(nun_package_t* out_results, u32 max_results);
int  nun_get_info(const char* pkg_name, nun_package_t* out_pkg);
int  nun_update_repo(void);
int  nun_cli(int argc, const char* const* argv, char* out_msg, u32 out_max);

#endif /* NUN_PKG_H */
