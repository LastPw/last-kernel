#ifndef DEVFS_H
#define DEVFS_H

#include "types.h"

#define DEVFS_MAX_NAME  32
#define DEVFS_MAX_DEVS  64

typedef enum {
    DEV_TYPE_NONE  = 0,
    DEV_TYPE_CHAR  = 1,   /* Character Device (TTY, Serial, Random) */
    DEV_TYPE_BLOCK = 2    /* Block Device (Storage Disks, Partitions) */
} dev_type_t;

/* Monolithic Device File Operations (Standard POSIX Function Pointers) */
typedef struct file_operations {
    int (*open)(const char* name, u32 flags, void** priv_data);
    i32 (*read)(void* priv_data, char* buffer, u32 count, u64 offset);
    i32 (*write)(void* priv_data, const char* buffer, u32 count, u64 offset);
    int (*ioctl)(void* priv_data, u32 cmd, u64 arg);
    int (*close)(void* priv_data);
    int (*poll)(void* priv_data, u32 events);
} file_operations_t;

typedef struct devfs_node {
    u8                 used;
    u8                 type;               /* DEV_TYPE_CHAR or DEV_TYPE_BLOCK */
    char               name[DEVFS_MAX_NAME];/* e.g. "tty0", "null", "zero", "sda" */
    file_operations_t  fops;
    void*              priv_data;
} devfs_node_t;

/* DevFS Subsystem Core APIs */
void devfs_init(void);
int devfs_register_device(const char* name, u8 dev_type, const file_operations_t* fops, void* priv);
int devfs_unregister_device(const char* name);
int devfs_open(const char* path, u32 flags, void** out_priv, const file_operations_t** out_fops);
int devfs_is_dev_path(const char* path);
int devfs_list(char* out_buf, u32 max_len);

#endif /* DEVFS_H */
