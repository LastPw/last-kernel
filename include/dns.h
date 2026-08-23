#ifndef DNS_H
#define DNS_H

#include "types.h"

typedef struct {
    u8 valid;
    u8 family;
    u16 reserved;
    char name[64];
    u8 address[16];
    u32 expires_tick;
} dns_cache_entry_t;

#define DNS_SERVER_MAX 3u

int dns_resolve_ipv4(const char* name, u32* out_ip, u32 timeout_ticks);
int dns_resolve_ipv6(const char* name, u8 out_ip[16], u32 timeout_ticks);
void dns_cache_clear(void);
int dns_cache_lookup(const char* name, u8 family, u8* out_address);
int dns_cache_get(u32 index, dns_cache_entry_t* out);
int dns_set_server(u32 index, u32 server_ip);
int dns_get_server(u32 index, u32* out_server_ip);
void dns_reset_servers(void);

#endif
