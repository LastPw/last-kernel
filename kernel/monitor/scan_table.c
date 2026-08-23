#include "scan_table.h"

static scan_entry_t table[SCAN_TABLE_MAX];
static u32 count = 0;

void scan_table_reset(void) {
    count = 0;
    for (u32 i = 0; i < SCAN_TABLE_MAX; i++) {
        table[i].target_ip = 0;
        table[i].port = 0;
        table[i].scan_type = 0;
        table[i].state = SCAN_STATE_PENDING;
        table[i].timestamp = 0;
        table[i].retries = 0;
    }
}

int scan_table_add(u32 target_ip, u16 port, u8 scan_type) {
    if (count >= SCAN_TABLE_MAX) {
        return -1;
    }
    table[count].target_ip = target_ip;
    table[count].port = port;
    table[count].scan_type = scan_type;
    table[count].state = SCAN_STATE_PENDING;
    table[count].timestamp = 0;
    table[count].retries = 0;
    count++;
    return (int)(count - 1);
}

scan_entry_t* scan_table_get(u32 index) {
    if (index >= count) {
        return 0;
    }
    return &table[index];
}

u32 scan_table_count(void) {
    return count;
}

int scan_table_find(u32 target_ip, u16 port, u8 scan_type) {
    for (u32 i = 0; i < count; i++) {
        if (table[i].target_ip == target_ip &&
            table[i].port == port &&
            table[i].scan_type == scan_type) {
            return (int)i;
        }
    }
    return -1;
}

u32 scan_table_state_count(u8 state) {
    u32 n = 0;
    for (u32 i = 0; i < count; i++) {
        if (table[i].state == state) {
            n++;
        }
    }
    return n;
}
