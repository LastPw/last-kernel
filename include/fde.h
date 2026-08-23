#ifndef FDE_H
#define FDE_H

#include "types.h"

/* ==================== Constants ==================== */

#define FDE_MAGIC              0x454C4446u  /* "FDLE" = Full Disk LastOS Encryption */
#define FDE_SALT_SIZE           16u
#define FDE_KEY_SIZE            64u         /* 32 bytes key1 + 32 bytes key2 for AES-256-XTS */
#define FDE_IV_SIZE             16u
#define FDE_HASH_SIZE           32u         /* SHA-256 */
#define FDE_SECTOR_SIZE         512u
#define FDE_BLOCK_SIZE          16u         /* AES block size */
#define FDE_BLOCKS_PER_SECTOR   (FDE_SECTOR_SIZE / FDE_BLOCK_SIZE)  /* 32 */
#define FDE_PBKDF2_ITERATIONS   10000u      /* Iterations for key derivation */
/* Accepted band for the on-disk iteration count. The header is attacker
 * controlled, so unlock must reject a 0 (single-HMAC) or absurdly large
 * (unbounded hang) value instead of feeding it to the KDF. */
#define FDE_PBKDF2_MIN_ITER     1000u
#define FDE_PBKDF2_MAX_ITER     100000u     /* Lowered from 1M to prevent DoS */
#define FDE_HEADER_SECTORS      8u          /* Header occupies 8 sectors (4KB) */
#define FDE_MAX_PASSWORD        64u

/* Status codes */
#define FDE_OK                  0
#define FDE_ERR_MAGIC           -1
#define FDE_ERR_AUTH            -2
#define FDE_ERR_IO              -3
#define FDE_ERR_MEM             -4
#define FDE_ERR_ALREADY         -5
#define FDE_ERR_NOT_INIT        -6

/* ==================== On-Disk Header Structure ==================== */

/* The FDE header is stored in the first FDE_HEADER_SECTORS sectors.
   Sector 0: Magic + salt + verification data + PBKDF2 parameters
   Sectors 1..7: Reserved for future use (anti-tamper, backup header) */
typedef struct {
    u32  magic;              /* FDE_MAGIC */
    u8   salt[FDE_SALT_SIZE];/* PBKDF2 salt */
    u32  pbkdf2_iterations;  /* PBKDF2 iteration count */
    u8   verifier[FDE_HASH_SIZE]; /* SHA-256(encrypted_zero_block) for password verification */
    u8   reserved[512 - 4 - 16 - 4 - 32]; /* Pad to 512 bytes */
    u8   reserved_sectors[FDE_SECTOR_SIZE * 7]; /* Reserved for future use */
} __attribute__((packed)) fde_header_t;

/* ==================== Engineered Key Material ==================== */

/* In-memory FDE state */
typedef struct {
    u8   active;             /* Is encryption active? */
    u8   initialized;        /* Has been initialized with a key? */
    u32  start_sector;       /* Data starts after header (typically FDE_HEADER_SECTORS) */
    u32  total_sectors;      /* Total encrypted sectors available */
    u8   key1[32];           /* AES-256 key for data encryption */
    u8   key2[32];           /* AES-256 key for tweak encryption */
    u32  dev_id;
    u32  start_lba;          /* Physical start LBA of the encrypted partition */
} fde_state_t;

/* ==================== API Functions ==================== */

/* Initialize FDE subsystem */
void fde_init(void);

/* Check if FDE is currently active */
int  fde_is_active(void);

/*
 * Format a disk with FDE: writes header with salt, derives keys from password,
 * encrypts a verification block. The disk must NOT already have FDE.
 * Returns FDE_OK on success.
 */
int  fde_format(u32 dev_id, u32 start_lba, u32 total_lba, const char* password);

/*
 * Unlock an FDE-encrypted disk: reads the header, derives keys from password,
 * and verifies against the stored verifier. If correct, activates encryption.
 * Returns FDE_OK on success, FDE_ERR_AUTH on wrong password.
 */
int  fde_unlock(u32 dev_id, u32 start_lba, const char* password);

/*
 * Lock the FDE device: clears keys from memory.
 */
void fde_lock(void);

/*
 * Read a sector through the FDE layer: reads physical sector, decrypts it.
 * lba is relative to the start of encrypted data (0 = first encrypted sector).
 */
int  fde_read_sector(u32 lba, u8* out512);

/*
 * Write a sector through the FDE layer: encrypts the data, writes physical sector.
 * lba is relative to the start of encrypted data (0 = first encrypted sector).
 */
int  fde_write_sector(u32 lba, const u8* in512);

/* Get FDE info */
int  fde_status(u32* out_total_sectors);

/* Get the absolute LBA range of encrypted data (for use in disk.c mapping) */
void fde_encrypted_range(u32* out_start_lba, u32* out_sector_count);

#endif /* FDE_H */
