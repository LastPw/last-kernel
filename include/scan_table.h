#ifndef SCAN_TABLE_H
#define SCAN_TABLE_H

#include "types.h"

#define SCAN_TABLE_MAX 1024

enum {
    SCAN_TYPE_SYN = 1,
    SCAN_TYPE_CONNECT = 2,
    SCAN_TYPE_UDP = 3
};

enum {
    SCAN_STATE_PENDING = 0,
    SCAN_STATE_SENT = 1,
    SCAN_STATE_OPEN = 2,
    SCAN_STATE_CLOSED = 3,
    SCAN_STATE_FILTERED = 4
};

typedef struct __attribute__((aligned(64))) scan_entry {
    u32 target_ip;
    u16 port;
    u8 scan_type;
    u8 state;
    u64 timestamp;
    u8 retries;
} scan_entry_t;

void scan_table_reset(void);
int scan_table_add(u32 target_ip, u16 port, u8 scan_type);
scan_entry_t* scan_table_get(u32 index);
u32 scan_table_count(void);
int scan_table_find(u32 target_ip, u16 port, u8 scan_type);
u32 scan_table_state_count(u8 state);

#endif
