#ifndef DRIVER_BUS_H
#define DRIVER_BUS_H

#include "types.h"

#define DRIVER_NAME_MAX 24
#define DRIVER_SLOT_MAX 32

typedef struct {
    int (*probe)(void);
    int (*start)(void);
    int (*stop)(void);
    int (*read)(u8* out, u32 len);
    int (*write)(const u8* in, u32 len);
    int (*ioctl)(u32 cmd, u32 arg);
} driver_ops_t;

typedef struct {
    char name[DRIVER_NAME_MAX];
    u8 installed;
    u8 active;
    u8 sandboxed;
    u8 critical;
    u8 probe_ok;
    u32 starts;
    u32 stops;
    u32 failures;
    u32 restart_count;
    u32 read_ops;
    u32 write_ops;
    u32 health_score;
} driver_info_t;

typedef struct {
    u32 installed;
    u32 active;
    u32 total_failures;
    u32 total_restarts;
    u32 avg_health;
} driver_bus_stats_t;

void driver_bus_init(void);
int driver_register(const char* name, const driver_ops_t* ops);
int driver_unregister(const char* name);
int driver_start(const char* name);
int driver_stop(const char* name);
int driver_ioctl(const char* name, u32 cmd, u32 arg);
int driver_read(const char* name, u8* out, u32 len);
int driver_write(const char* name, const u8* in, u32 len);
int driver_recover(const char* name);
int driver_set_flags(const char* name, u8 sandboxed, u8 critical);
int driver_probe_all(void);
u32 driver_count(void);
int driver_info_by_index(u32 idx, driver_info_t* out);
void driver_bus_stats(driver_bus_stats_t* out);

#endif
