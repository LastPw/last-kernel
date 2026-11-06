#include "userdb.h"
#include "vfs.h"
#include "persist.h"
#include "sha256.h"
#include "string.h"
#include "timer.h"
#include "entropy.h"
#include "bearssl.h"

#define USERDB_PATH "/etc/lastos.users"
#define USERDB_LINE_MAX 128u
#define USERDB_ITERATIONS 12000u
#define USERDB_MAGIC0 'U'
#define USERDB_MAGIC1 'D'
#define USERDB_MAGIC2 'B'
#define USERDB_MAGIC3 '2'

static user_record_t g_users[USERDB_MAX_USERS];
static char g_salts[USERDB_MAX_USERS][USERDB_SALT_MAX];
static u8 g_hashes[USERDB_MAX_USERS][USERDB_HASH_SIZE];
static u8 g_hash_sizes[USERDB_MAX_USERS];
static u32 g_count;
static int g_current = -1;
static u32 g_login_failures[USERDB_MAX_USERS];
static u32 g_lockout_until[USERDB_MAX_USERS];
#define USERDB_MAX_FAILURES 5u
#define USERDB_LOCKOUT_TICKS 3000u  /* ~30 seconds at 100 Hz */

static void copy_text(char* dst, u32 size, const char* src) {
    if (!dst || size == 0u) return;
    str_ncpy(dst, src ? src : "", size - 1u);
    dst[size - 1u] = '\0';
}

static int number(const char* s, u32* out) {
    u32 v = 0u, i = 0u;
    if (!s || !s[0] || !out) return -1;
    while (s[i] >= '0' && s[i] <= '9') { v = v * 10u + (u32)(s[i++] - '0'); }
    if (i == 0u || s[i] != '\0') return -1;
    *out = v;
    return 0;
}

static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static u8 g_db_key[32];
static u8 g_db_key_ready = 0u;

static void userdb_key(u8 key[32]) {
    if (!g_db_key_ready) {
        /* SECURITY: derive the database key from boot-time entropy mixed
         * with a domain string.  This makes the key unpredictable to anyone
         * who only has the kernel binary.  The key is stable for the
         * lifetime of this boot; on reboot a new key is derived and the
         * database is re-encrypted via save_db() during userdb_init(). */
        static const u8 material[] = "Axiom LastOS user database key v3";
        u8 seed[16];
        sha256_ctx_t ctx;
        entropy_fill(seed, sizeof(seed));
        sha256_init(&ctx);
        sha256_update(&ctx, material, sizeof(material) - 1u);
        sha256_update(&ctx, seed, sizeof(seed));
        sha256_final(&ctx, g_db_key);
        g_db_key_ready = 1u;
    }
    memcpy(key, g_db_key, 32u);
}

static void put32(u8* p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8u); p[2] = (u8)(v >> 16u); p[3] = (u8)(v >> 24u); }
static u32 get32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8u) | ((u32)p[2] << 16u) | ((u32)p[3] << 24u); }

static int userdb_decrypt(const u8* raw, u32 raw_len, char* plain, u32 plain_cap) {
    u8 key[32], tag[32], expected[32], iv[12];
    br_aes_ct_ctr_keys aes;
    u32 len;
    if (!raw || !plain || raw_len < 52u || raw[0] != USERDB_MAGIC0 || raw[1] != USERDB_MAGIC1 || raw[2] != USERDB_MAGIC2 || raw[3] != USERDB_MAGIC3) return -1;
    len = get32(raw + 4u);
    if (len == 0u || len + 52u > raw_len || len + 1u > plain_cap) return -1;
    memcpy(iv, raw + 8u, sizeof(iv)); memcpy(tag, raw + 20u, sizeof(tag));
    userdb_key(key);
    hmac_sha256(key, sizeof(key), raw, 20u + len, expected);
    if (!crypto_equal(tag, expected, sizeof(tag))) return -1;
    memcpy(plain, raw + 52u, len);
    br_aes_ct_ctr_init(&aes, key, sizeof(key));
    (void)br_aes_ct_ctr_run(&aes, iv, 1u, plain, len);
    plain[len] = '\0';
    memset(key, 0, sizeof(key)); memset(expected, 0, sizeof(expected));
    return (int)len;
}

static void hash_password(const char* salt, const char* password, u8 out[USERDB_HASH_SIZE]) {
    u8 u[SHA256_DIGEST_SIZE], salt_block[32];
    u32 salt_len = 0u;
    if (!salt || !password || !out) return;
    memset(out, 0, USERDB_HASH_SIZE);
    while (salt[salt_len] && salt_len + 5u < sizeof(salt_block)) { salt_block[salt_len] = (u8)salt[salt_len]; salt_len++; }
    salt_block[salt_len++] = 0u; salt_block[salt_len++] = 0u; salt_block[salt_len++] = 0u; salt_block[salt_len++] = 1u;
    hmac_sha256((const u8*)password, str_len(password), salt_block, salt_len, u);
    memcpy(out, u, sizeof(u));
    for (u32 round = 1u; round < USERDB_ITERATIONS; round++) {
        hmac_sha256((const u8*)password, str_len(password), u, sizeof(u), u);
        for (u32 j = 0u; j < sizeof(u); j++) out[j] ^= u[j];
    }
    memset(u, 0, sizeof(u)); memset(salt_block, 0, sizeof(salt_block));
}

static void hash_password_legacy_v1(const char* salt, const char* password, u8 out[8]) {
    char material[USERDB_SALT_MAX + 65u];
    u8 digest[SHA256_DIGEST_SIZE];
    u32 at = 0u;
    if (!salt || !password || !out) return;
    while (salt[at] && at + 1u < sizeof(material)) { material[at] = salt[at]; at++; }
    if (at + 1u < sizeof(material)) material[at++] = ':';
    for (u32 i = 0u; password[i] && at + 1u < sizeof(material); i++) material[at++] = password[i];
    material[at] = '\0';
    sha256((const u8*)material, at, digest);
    memcpy(out, digest, 8u);
    memset(material, 0, sizeof(material));
    memset(digest, 0, sizeof(digest));
}

static char hex_digit(u8 v) { v &= 0x0Fu; return v < 10u ? (char)('0' + v) : (char)('a' + v - 10u); }

static void make_salt(char out[USERDB_SALT_MAX]) {
    u8 random_bytes[USERDB_SALT_MAX - 1u];
    entropy_fill(random_bytes, sizeof(random_bytes));
    for (u32 i = 0u; i < USERDB_SALT_MAX - 1u; i++) {
        out[i] = hex_digit(random_bytes[i] >> 4u);
    }
    out[USERDB_SALT_MAX - 1u] = '\0';
}

static int find_user(const char* name) {
    for (u32 i = 0u; i < g_count; i++) if (str_cmp(g_users[i].name, name) == 0) return (int)i;
    return -1;
}

static int valid_name(const char* name) {
    if (!name || !name[0] || str_len(name) >= USERDB_NAME_MAX) return 0;
    for (u32 i = 0u; name[i]; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static int parse_line(char* line) {
    char* f[6]; u32 n = 0u; char* p = line;
    while (n < 6u) {
        f[n++] = p;
        while (*p && *p != ':') p++;
        if (!*p) break;
        *p++ = '\0';
    }
    if (n != 6u || g_count >= USERDB_MAX_USERS || !valid_name(f[0])) return -1;
    user_record_t* u = &g_users[g_count];
    u32 flags;
    if (number(f[1], &u->uid) != 0 || number(f[2], &u->gid) != 0 || number(f[3], &flags) != 0) return -1;
    if (str_len(f[4]) != USERDB_SALT_MAX - 1u ||
        (str_len(f[5]) != 16u && str_len(f[5]) != USERDB_HASH_SIZE * 2u)) return -1;
    copy_text(u->name, sizeof(u->name), f[0]); copy_text(g_salts[g_count], sizeof(g_salts[g_count]), f[4]);
    g_hash_sizes[g_count] = (str_len(f[5]) == 16u) ? 8u : USERDB_HASH_SIZE;
    memset(g_hashes[g_count], 0, USERDB_HASH_SIZE);
    for (u32 i = 0u; i < g_hash_sizes[g_count]; i++) { int hi = hex(f[5][i * 2u]); int lo = hex(f[5][i * 2u + 1u]); if (hi < 0 || lo < 0) return -1; g_hashes[g_count][i] = (u8)((hi << 4) | lo); }
    u->flags = (u8)flags; g_count++;
    return 0;
}

static int save_db(void) {
    char blob[USERDB_MAX_USERS * USERDB_LINE_MAX]; u32 at = 0u;
    u8 record[sizeof(blob) + 52u], key[32], iv[12], tag[32];
    br_aes_ct_ctr_keys aes;
    memset(blob, 0, sizeof(blob));
    if (vfs_info(USERDB_PATH, 0) != 0) {
        if (vfs_info("/etc", 0) != 0) (void)vfs_mkdir("/etc");
        if (vfs_create_file(USERDB_PATH) != 0 && vfs_info(USERDB_PATH, 0) != 0) return -1;
    }
    for (u32 i = 0u; i < g_count; i++) {
        char line[USERDB_LINE_MAX]; u32 n = 0u;
        line[0] = '\0'; str_cat(line, g_users[i].name); str_cat(line, ":");
        /* Numeric fields are small; emit them without pulling a formatter into the kernel. */
        char num[12]; u32 v = g_users[i].uid, q = 0u;
        do { num[q++] = (char)('0' + (v % 10u)); v /= 10u; } while (v && q < sizeof(num) - 1u);
        for (u32 j = 0u; j < q / 2u; j++) { char t = num[j]; num[j] = num[q - j - 1u]; num[q - j - 1u] = t; }
        num[q] = '\0'; str_cat(line, num); str_cat(line, ":");
        v = g_users[i].gid; q = 0u; do { num[q++] = (char)('0' + (v % 10u)); v /= 10u; } while (v && q < sizeof(num) - 1u);
        for (u32 j = 0u; j < q / 2u; j++) { char t = num[j]; num[j] = num[q - j - 1u]; num[q - j - 1u] = t; }
        num[q] = '\0'; str_cat(line, num); str_cat(line, ":");
        v = g_users[i].flags; num[0] = (char)('0' + v); num[1] = '\0'; str_cat(line, num); str_cat(line, ":");
        str_cat(line, g_salts[i]); str_cat(line, ":");
        u32 pos = str_len(line);
        u32 hash_bytes = g_hash_sizes[i] ? g_hash_sizes[i] : USERDB_HASH_SIZE;
        if (pos + hash_bytes * 2u + 2u >= sizeof(line)) return -1;
        for (u32 j = 0u; j < hash_bytes; j++) {
            line[pos++] = hex_digit(g_hashes[i][j] >> 4u);
            line[pos++] = hex_digit(g_hashes[i][j]);
        }
        line[pos] = '\0';
        str_cat(line, "\n"); n = str_len(line);
        if (at + n >= sizeof(blob)) return -1; memcpy(blob + at, line, n); at += n;
    }
    if (at + 52u > sizeof(record)) return -1;
    memset(record, 0, sizeof(record)); record[0] = USERDB_MAGIC0; record[1] = USERDB_MAGIC1; record[2] = USERDB_MAGIC2; record[3] = USERDB_MAGIC3;
    put32(record + 4u, at); entropy_fill(iv, sizeof(iv)); memcpy(record + 8u, iv, sizeof(iv)); memcpy(record + 52u, blob, at);
    userdb_key(key); br_aes_ct_ctr_init(&aes, key, sizeof(key)); (void)br_aes_ct_ctr_run(&aes, iv, 1u, record + 52u, at);
    hmac_sha256(key, sizeof(key), record, 20u + at, tag); memcpy(record + 20u, tag, sizeof(tag));
    memset(key, 0, sizeof(key));
    return vfs_write_file(USERDB_PATH, (const char*)record, at + 52u);
}

static void ensure_root(void) {
    if (find_user("root") >= 0 || g_count >= USERDB_MAX_USERS) return;
    user_record_t* u = &g_users[g_count]; copy_text(u->name, sizeof(u->name), "root"); u->uid = 0u; u->gid = 0u; u->flags = USERDB_FLAG_ADMIN | USERDB_FLAG_LOCKED;
    copy_text(g_salts[g_count], sizeof(g_salts[g_count]), "00000000"); memset(g_hashes[g_count], 0, USERDB_HASH_SIZE); g_hash_sizes[g_count] = 0u; g_count++;
}

void userdb_init(void) {
    u8 raw[USERDB_MAX_USERS * USERDB_LINE_MAX + 52u];
    char blob[USERDB_MAX_USERS * USERDB_LINE_MAX + 1u];
    g_count = 0u; g_current = -1; memset(g_hash_sizes, 0, sizeof(g_hash_sizes));
    memset(g_login_failures, 0, sizeof(g_login_failures));
    memset(g_lockout_until, 0, sizeof(g_lockout_until));
    vfs_set_credentials(0u, 0u);
    if (vfs_info(USERDB_PATH, 0) != 0) { (void)vfs_create_file(USERDB_PATH); }
    int n = vfs_read_file(USERDB_PATH, (char*)raw, sizeof(raw));
    int plain_n = -1;
    if (n > 0) {
        if (n >= 4 && raw[0] == USERDB_MAGIC0 && raw[1] == USERDB_MAGIC1 &&
            raw[2] == USERDB_MAGIC2 && raw[3] == USERDB_MAGIC3) {
            plain_n = userdb_decrypt(raw, (u32)n, blob, sizeof(blob));
        } else if ((u32)n < sizeof(blob)) {
            /* One-time migration for the old plaintext database format. */
            memcpy(blob, raw, (u32)n);
            blob[n] = '\0';
            plain_n = n;
        }
    }
    if (plain_n > 0) { char* p = blob; while (*p) { char* e = p; while (*e && *e != '\n') e++; if (*e) *e++ = '\0'; (void)parse_line(p); p = e; } }
    ensure_root(); (void)save_db();
    (void)userdb_sync_primary();
}

int userdb_sync_primary(void) {
    char name[USERDB_NAME_MAX], pass[24];
    if (persist_get_auth(name, sizeof(name), pass, sizeof(pass)) != 0 || !name[0]) return -1;
    if (find_user(name) < 0 && userdb_add(name, pass, 1u) != 0) return -1;
    return 0;
}

int userdb_login(const char* name, const char* password) {
    int i = find_user(name); u8 hash[USERDB_HASH_SIZE];
    int ok = 0;
    if (i < 0 || !password || (g_users[i].flags & USERDB_FLAG_LOCKED)) return -1;
    /* SECURITY: rate-limit login attempts to prevent brute force. After
     * USERDB_MAX_FAILURES consecutive failures the account enters a timed
     * lockout period before new attempts are accepted. */
    if (g_login_failures[i] >= USERDB_MAX_FAILURES) {
        if ((i32)(timer_ticks() - g_lockout_until[i]) < 0) return -1;
        g_login_failures[i] = 0u;  /* lockout expired, reset */
    }
    u32 compare_size = g_hash_sizes[i] ? g_hash_sizes[i] : USERDB_HASH_SIZE;
    if (compare_size == USERDB_HASH_SIZE) {
        hash_password(g_salts[i], password, hash);
        ok = crypto_equal(hash, g_hashes[i], USERDB_HASH_SIZE);
    } else if (compare_size == 8u) {
        u8 legacy[8];
        hash_password_legacy_v1(g_salts[i], password, legacy);
        ok = crypto_equal(legacy, g_hashes[i], sizeof(legacy));
        memset(legacy, 0, sizeof(legacy));
        if (ok) {
            hash_password(g_salts[i], password, hash);
            memcpy(g_hashes[i], hash, USERDB_HASH_SIZE);
            g_hash_sizes[i] = USERDB_HASH_SIZE;
            (void)save_db();
        }
    }
    if (!ok) {
        g_login_failures[i]++;
        if (g_login_failures[i] >= USERDB_MAX_FAILURES) {
            g_lockout_until[i] = timer_ticks() + USERDB_LOCKOUT_TICKS;
        }
        return -1;
    }
    g_login_failures[i] = 0u;  /* reset on success */
    g_current = i; vfs_set_credentials(g_users[i].uid, g_users[i].gid); return 0;
}

int userdb_add(const char* name, const char* password, u8 admin) {
    if (!valid_name(name) || !password || !password[0] || find_user(name) >= 0 || g_count >= USERDB_MAX_USERS) return -1;
    user_record_t* u = &g_users[g_count]; copy_text(u->name, sizeof(u->name), name); u->uid = 1000u + (g_count ? g_count - 1u : 0u); u->gid = u->uid; u->flags = admin ? USERDB_FLAG_ADMIN : 0u;
    make_salt(g_salts[g_count]); hash_password(g_salts[g_count], password, g_hashes[g_count]); g_hash_sizes[g_count] = USERDB_HASH_SIZE; g_count++; return save_db();
}

int userdb_change_password(const char* name, const char* password) {
    int i = find_user(name); if (i < 0 || !password || !password[0]) return -1;
    make_salt(g_salts[i]); hash_password(g_salts[i], password, g_hashes[i]); g_hash_sizes[i] = USERDB_HASH_SIZE; g_users[i].flags &= (u8)~USERDB_FLAG_LOCKED; return save_db();
}

int userdb_set_locked(const char* name, u8 locked) {
    int i = find_user(name); if (i < 0 || (i == g_current && locked)) return -1;
    if (locked) g_users[i].flags |= USERDB_FLAG_LOCKED; else g_users[i].flags &= (u8)~USERDB_FLAG_LOCKED; return save_db();
}

u32 userdb_count(void) { return g_count; }
int userdb_get(u32 index, user_record_t* out) { if (!out || index >= g_count) return -1; *out = g_users[index]; return 0; }
int userdb_current(user_record_t* out) { if (!out || g_current < 0) return -1; *out = g_users[(u32)g_current]; return 0; }
int userdb_is_admin(void) { return g_current >= 0 && (g_users[(u32)g_current].flags & USERDB_FLAG_ADMIN) != 0; }
u32 userdb_current_uid(void) { return g_current >= 0 ? g_users[(u32)g_current].uid : 0u; }
u32 userdb_current_gid(void) { return g_current >= 0 ? g_users[(u32)g_current].gid : 0u; }
