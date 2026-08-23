#ifndef USERDB_H
#define USERDB_H

#include "types.h"

#define USERDB_NAME_MAX 24u
#define USERDB_SALT_MAX 9u
#define USERDB_HASH_SIZE 32u
#define USERDB_MAX_USERS 16u
#define USERDB_FLAG_ADMIN 0x01u
#define USERDB_FLAG_LOCKED 0x02u

typedef struct {
    char name[USERDB_NAME_MAX];
    u32 uid;
    u32 gid;
    u8 flags;
} user_record_t;

void userdb_init(void);
int userdb_sync_primary(void);
int userdb_login(const char* name, const char* password);
int userdb_add(const char* name, const char* password, u8 admin);
int userdb_change_password(const char* name, const char* password);
int userdb_set_locked(const char* name, u8 locked);
u32 userdb_count(void);
int userdb_get(u32 index, user_record_t* out);
int userdb_current(user_record_t* out);
int userdb_is_admin(void);
u32 userdb_current_uid(void);
u32 userdb_current_gid(void);

#endif
