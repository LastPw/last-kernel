#ifndef LASTOS_WIREGUARD_H
#define LASTOS_WIREGUARD_H

#include "types.h"

typedef struct {
    u8 configured;
    u8 active;
    u16 endpoint_port;
    char endpoint[64];
    u8 private_key[32];
    u8 peer_key[32];
    u32 allowed_network;
    u32 allowed_mask;
} wireguard_status_t;

int wireguard_configure(const char* endpoint, u16 port, const u8 private_key[32],
                        const u8 peer_key[32], u32 allowed_network, u32 allowed_mask);
void wireguard_clear(void);
int wireguard_start(void);
int wireguard_stop(void);
void wireguard_status(wireguard_status_t* out);

#endif
