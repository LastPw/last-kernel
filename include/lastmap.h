#ifndef LASTMAP_H
#define LASTMAP_H

#include "types.h"

typedef struct {
    u32 target_ip;
    u16 port_start;
    u16 port_end;
    u8 scan_type;
    u8 timing_template;
    u8 randomize;
    u8 banner;
    u8 show_stats;
    u8 aggressive;
    u8 cores_auto;
    u8 cores;
    u32 rate_pps;
} lastmap_config_t;

void lastmap_init(void);
void lastmap_tick(void);
int lastmap_command(const char* args);
int lastmap_start(const lastmap_config_t* cfg);
void lastmap_print_stats(void);
void lastmap_on_tcp(u32 src_ip, u16 src_port, u16 dst_port, u8 flags);
void lastmap_on_udp(u32 src_ip, u16 src_port, u16 dst_port, const u8* payload, u16 len);

#endif
