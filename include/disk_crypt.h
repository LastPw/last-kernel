#ifndef DISK_CRYPT_H
#define DISK_CRYPT_H

#include "types.h"
#include "disk.h"

#define DISK_CRYPT_KEY_SIZE 32u   /* 256-bit AES key */
#define DISK_CRYPT_IV_SIZE  16u

/* Per-sector encryption using AES-256-CTR.
 * Key is derived from a passphrase using SHA256.
 * Each sector gets a unique IV: SHA256(sector_lba || key)[0:16] */

int disk_crypt_init(const u8* passphrase, u32 passphrase_len);
int disk_crypt_enable(void);
int disk_crypt_disable(void);
int disk_crypt_active(void);
int disk_crypt_change_key(const u8* passphrase, u32 passphrase_len);

/* Encrypted wrappers around disk_read_sector/disk_write_sector */
int disk_crypt_read_sector(u32 lba, u8* out512);
int disk_crypt_write_sector(u32 lba, const u8* in512);

#endif
