#include "persist.h"
#include "persistent_disk.h"
#include "io.h"
#include "string.h"
#include "antivirus.h"
#include "sha256.h"
#include "timer.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

#define PERSIST_BASE 0x40
#define PERSIST_END  0x7F
#define PREF_BASE    0x28
#define PREF_SIG0    'P'
#define PREF_SIG1    'F'

#define OFF_SIG0      0
#define OFF_SIG1      1
#define OFF_VERSION   2
#define OFF_DLEN      3
#define OFF_BOOT0     4
#define OFF_BOOT1     5
#define OFF_BOOT2     6
#define OFF_BOOT3     7
#define OFF_DATA      8
#define OFF_INSTALL_LOCK (OFF_DATA + PERSIST_MAX_DATA)

#define SIG0 'L'
#define SIG1 'O'
#define VERSION 1
#define INSTALL_LOCK_MAGIC 0xA7u

static char g_saved_data[PERSIST_MAX_DATA + 1];
static char g_auth_user[24];
static char g_auth_pass[24];
static char g_auth_salt[9];
static u8 g_auth_hash[8];
static u8 g_auth_hashed;
static char g_hostname[32];

static u8 cmos_read(u8 idx) {
    outb(CMOS_ADDR, (u8)(0x80 | idx));
    return inb(CMOS_DATA);
}

static void cmos_write(u8 idx, u8 value) {
    outb(CMOS_ADDR, (u8)(0x80 | idx));
    outb(CMOS_DATA, value);
}

static u8 read_off(u8 off) {
    return cmos_read((u8)(PERSIST_BASE + off));
}

static void write_off(u8 off, u8 v) {
    cmos_write((u8)(PERSIST_BASE + off), v);
}

static void write_u32(u8 off, u32 v) {
    write_off((u8)(off + 0), (u8)(v & 0xFF));
    write_off((u8)(off + 1), (u8)((v >> 8) & 0xFF));
    write_off((u8)(off + 2), (u8)((v >> 16) & 0xFF));
    write_off((u8)(off + 3), (u8)((v >> 24) & 0xFF));
}

static u32 read_u32(u8 off) {
    u32 b0 = read_off((u8)(off + 0));
    u32 b1 = read_off((u8)(off + 1));
    u32 b2 = read_off((u8)(off + 2));
    u32 b3 = read_off((u8)(off + 3));
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static int header_ok(void) {
    return read_off(OFF_SIG0) == SIG0 &&
           read_off(OFF_SIG1) == SIG1 &&
           read_off(OFF_VERSION) == VERSION;
}

static void safe_copy(char* dst, u32 dst_sz, const char* src) {
    u32 i = 0u;
    if (!dst || dst_sz == 0u) return;
    if (!src) { dst[0] = '\0'; return; }
    while (src[i] && i + 1u < dst_sz) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char hex_digit(u8 value) {
    value &= 0x0Fu;
    return value < 10u ? (char)('0' + value) : (char)('a' + value - 10u);
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void auth_hash(const char* salt, const char* pass, u8 out[8]) {
    u32 a = 2166136261u;
    u32 b = 0x9E3779B9u;
    u32 i;
    if (!salt || !pass || !out) return;
    for (i = 0u; salt[i]; i++) {
        a ^= (u8)salt[i]; a *= 16777619u;
        b ^= a + (u32)(u8)salt[i]; b = (b << 7u) | (b >> 25u);
    }
    for (i = 0u; pass[i]; i++) {
        a ^= (u8)pass[i]; a *= 16777619u;
        b ^= a + (u32)(u8)pass[i]; b = (b << 9u) | (b >> 23u);
    }
    for (i = 0u; i < 4096u; i++) {
        a ^= b + i; a *= 2246822519u;
        b ^= a >> 13u; b *= 3266489917u;
    }
    out[0] = (u8)a; out[1] = (u8)(a >> 8u); out[2] = (u8)(a >> 16u); out[3] = (u8)(a >> 24u);
    out[4] = (u8)b; out[5] = (u8)(b >> 8u); out[6] = (u8)(b >> 16u); out[7] = (u8)(b >> 24u);
}

static void auth_make_salt(char out[9]) {
    u32 value = timer_ticks() ^ ((u32)inb(0x40u) << 16u) ^ read_u32(OFF_BOOT0) ^ 0xA84F23D1u;
    for (u32 i = 0u; i < 8u; i++) {
        value ^= value << 13u;
        value ^= value >> 17u;
        value ^= value << 5u;
        out[i] = hex_digit((u8)value);
    }
    out[8] = '\0';
}

static const char* skip_ws(const char* s) {
    while (s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++;
    return s ? s : "";
}

static void parse_blob(const char* blob) {
    const char* p = blob;
    g_saved_data[0] = '\0';
    g_auth_user[0] = '\0';
    g_auth_pass[0] = '\0';
    g_auth_salt[0] = '\0';
    g_hostname[0] = '\0';
    memset(g_auth_hash, 0, sizeof(g_auth_hash));
    g_auth_hashed = 0u;
    if (!blob || !blob[0]) return;
    p = skip_ws(p);
    if (p[0] == 'U' && p[1] == '=') {
        u32 i = 0u;
        p += 2;
        while (*p && *p != '\n' && i + 1u < sizeof(g_auth_user)) g_auth_user[i++] = *p++;
        g_auth_user[i] = '\0';
        if (*p == '\n') p++;
    }
    if (p[0] == 'N' && p[1] == '=') {
        u32 i = 0u;
        p += 2;
        while (*p && *p != '\n' && i + 1u < sizeof(g_hostname)) g_hostname[i++] = *p++;
        g_hostname[i] = '\0';
        if (*p == '\n') p++;
    }
    if (p[0] == 'P' && p[1] == '=') {
        u32 i = 0u;
        p += 2;
        while (*p && *p != '\n' && i + 1u < sizeof(g_auth_pass)) g_auth_pass[i++] = *p++;
        g_auth_pass[i] = '\0';
        if (*p == '\n') p++;
    }
    if (p[0] == 'S' && p[1] == '=') {
        u32 i = 0u;
        p += 2;
        while (*p && *p != '\n' && i + 1u < sizeof(g_auth_salt)) g_auth_salt[i++] = *p++;
        g_auth_salt[i] = '\0';
        if (*p == '\n') p++;
    }
    if (p[0] == 'H' && p[1] == '=') {
        u32 i = 0u;
        p += 2;
        while (i < sizeof(g_auth_hash) && p[0] && p[1]) {
            int hi = hex_value(p[0]);
            int lo = hex_value(p[1]);
            if (hi < 0 || lo < 0) break;
            g_auth_hash[i++] = (u8)((hi << 4) | lo);
            p += 2;
        }
        g_auth_hashed = (i == sizeof(g_auth_hash) && g_auth_salt[0]) ? 1u : 0u;
    }
    if (p[0] == 'D' && p[1] == '=') {
        u32 i = 0u;
        p += 2;
        while (*p && i + 1u < sizeof(g_saved_data)) g_saved_data[i++] = *p++;
        g_saved_data[i] = '\0';
    } else {
        safe_copy(g_saved_data, sizeof(g_saved_data), p);
    }

}

static void build_blob(char* out, u32 out_sz) {
    u32 i = 0u;
    u32 k = 0u;
    if (!out || out_sz == 0u) return;
    out[0] = '\0';

    if (i + 2u < out_sz) { out[i++] = 'U'; out[i++] = '='; }
    for (k = 0u; g_auth_user[k] && i + 1u < out_sz; k++) out[i++] = g_auth_user[k];
    if (i + 1u < out_sz) out[i++] = '\n';

    if (i + 2u < out_sz) { out[i++] = 'N'; out[i++] = '='; }
    for (k = 0u; g_hostname[k] && i + 1u < out_sz; k++) out[i++] = g_hostname[k];
    if (i + 1u < out_sz) out[i++] = '\n';

    if (g_auth_hashed) {
        if (i + 2u < out_sz) { out[i++] = 'S'; out[i++] = '='; }
        for (k = 0u; g_auth_salt[k] && i + 1u < out_sz; k++) out[i++] = g_auth_salt[k];
        if (i + 1u < out_sz) out[i++] = '\n';
        if (i + 2u < out_sz) { out[i++] = 'H'; out[i++] = '='; }
        for (k = 0u; k < sizeof(g_auth_hash) && i + 2u < out_sz; k++) {
            out[i++] = hex_digit((u8)(g_auth_hash[k] >> 4u));
            out[i++] = hex_digit(g_auth_hash[k]);
        }
        if (i + 1u < out_sz) out[i++] = '\n';
    } else {
        if (i + 2u < out_sz) { out[i++] = 'P'; out[i++] = '='; }
        for (k = 0u; g_auth_pass[k] && i + 1u < out_sz; k++) out[i++] = g_auth_pass[k];
        if (i + 1u < out_sz) out[i++] = '\n';
    }

    if (i + 2u < out_sz) { out[i++] = 'D'; out[i++] = '='; }
    for (k = 0u; g_saved_data[k] && i + 1u < out_sz; k++) out[i++] = g_saved_data[k];
    out[i] = '\0';
}

static int persist_disk_flush_blob(const char* blob, u32 len) {
    if (!pdisk_ready()) return -1;
    return pdisk_save_blob(blob, len);
}

void persist_load_disk(void) {
    if (!pdisk_ready()) return;
    char raw[PERSIST_MAX_DATA + 1];
    if (pdisk_load_blob(raw, sizeof(raw)) == 0 && raw[0]) {
        parse_blob(raw);
    }
}

static void persist_flush_blob(void) {
    char blob[PERSIST_MAX_DATA + 1];
    u32 len;
    build_blob(blob, sizeof(blob));
    len = str_len(blob);
    if (len > PERSIST_MAX_DATA) len = PERSIST_MAX_DATA;
    if (persist_disk_flush_blob(blob, len) == 0) {
        return;
    }
    write_off(OFF_DLEN, (u8)len);
    for (u32 i = 0; i < PERSIST_MAX_DATA; i++) {
        u8 ch = 0;
        if (i < len) ch = (u8)blob[i];
        write_off((u8)(OFF_DATA + i), ch);
    }
}

void persist_init(void) {
    if (!header_ok()) {
        write_off(OFF_SIG0, SIG0);
        write_off(OFF_SIG1, SIG1);
        write_off(OFF_VERSION, VERSION);
        write_off(OFF_DLEN, 0);
        write_u32(OFF_BOOT0, 0);
        for (u8 i = OFF_DATA; (u16)(PERSIST_BASE + i) <= PERSIST_END; i++) {
            write_off(i, 0);
        }
    }

    {
        char raw[PERSIST_MAX_DATA + 1];
        u32 len = read_off(OFF_DLEN);
        if (len > PERSIST_MAX_DATA) len = PERSIST_MAX_DATA;
        for (u32 i = 0u; i < len; i++) raw[i] = (char)read_off((u8)(OFF_DATA + i));
        raw[len] = '\0';
        parse_blob(raw);
    }

    if (cmos_read((u8)(PREF_BASE + 0u)) != PREF_SIG0 ||
        cmos_read((u8)(PREF_BASE + 1u)) != PREF_SIG1) {
        persist_set_ui_prefs(0u, 0u, 1u, 0u, 0x0Au, 0x0Bu, 0x0Au, 0x0Cu, 0x0Fu);
    }

    u32 boots = read_u32(OFF_BOOT0);
    write_u32(OFF_BOOT0, boots + 1);
}

int persist_install_locked(void) {
    return header_ok() && read_off(OFF_INSTALL_LOCK) == INSTALL_LOCK_MAGIC ? 1 : 0;
}

int persist_lock_installation(void) {
    if (!header_ok()) return -1;
    write_off(OFF_INSTALL_LOCK, INSTALL_LOCK_MAGIC);
    return persist_install_locked() ? 0 : -1;
}

void persist_save_data(const char* text) {
    if (!text) return;
    safe_copy(g_saved_data, sizeof(g_saved_data), text);
    persist_flush_blob();
    antivirus_on_file_event("nvram.persist", (const u8*)g_saved_data, str_len(g_saved_data), 0);
}

void persist_load_data(char* out, u32 out_size) {
    if (!out || out_size == 0) {
        return;
    }

    if (!header_ok()) {
        out[0] = '\0';
        return;
    }

    safe_copy(out, out_size, g_saved_data);
}

u32 persist_boot_count(void) {
    if (!header_ok()) {
        return 0;
    }
    return read_u32(OFF_BOOT0);
}

int persist_auth_exists(void) {
    return g_auth_user[0] && (g_auth_hashed || g_auth_pass[0]);
}

void persist_set_auth(const char* user, const char* pass) {
    if (!user || !pass) return;
    safe_copy(g_auth_user, sizeof(g_auth_user), user);
    g_auth_pass[0] = '\0';
    auth_make_salt(g_auth_salt);
    auth_hash(g_auth_salt, pass, g_auth_hash);
    g_auth_hashed = 1u;
    persist_flush_blob();
    antivirus_on_file_event("nvram.auth", (const u8*)g_auth_user, str_len(g_auth_user), 0);
}

int persist_get_auth(char* user, u32 user_size, char* pass, u32 pass_size) {
    if (!persist_auth_exists()) return -1;
    if (user) safe_copy(user, user_size, g_auth_user);
    if (pass) safe_copy(pass, pass_size, g_auth_pass);
    return 0;
}

int persist_auth_verify(const char* user, const char* pass) {
    u8 candidate[8];
    if (!user || !pass || !persist_auth_exists() || str_cmp(user, g_auth_user) != 0) return -1;
    if (!g_auth_hashed) return str_cmp(pass, g_auth_pass) == 0 ? 0 : -1;
    auth_hash(g_auth_salt, pass, candidate);
    return crypto_equal(candidate, g_auth_hash, sizeof(candidate)) ? 0 : -1;
}

void persist_set_hostname(const char* hostname) {
    if (!hostname || !hostname[0]) return;
    safe_copy(g_hostname, sizeof(g_hostname), hostname);
    persist_flush_blob();
}

int persist_get_hostname(char* out, u32 out_size) {
    if (!out || out_size == 0u || !g_hostname[0]) return -1;
    safe_copy(out, out_size, g_hostname);
    return 0;
}

void persist_set_ui_prefs(
    u8 theme, u8 bg_preset, u8 animations, u8 high_contrast,
    u8 prompt_color, u8 input_color, u8 ok_color, u8 err_color, u8 text_color
) {
    cmos_write((u8)(PREF_BASE + 0u), PREF_SIG0);
    cmos_write((u8)(PREF_BASE + 1u), PREF_SIG1);
    cmos_write((u8)(PREF_BASE + 2u), theme);
    cmos_write((u8)(PREF_BASE + 3u), bg_preset);
    cmos_write((u8)(PREF_BASE + 4u), animations ? 1u : 0u);
    cmos_write((u8)(PREF_BASE + 5u), high_contrast ? 1u : 0u);
    cmos_write((u8)(PREF_BASE + 6u), prompt_color);
    cmos_write((u8)(PREF_BASE + 7u), input_color);
    cmos_write((u8)(PREF_BASE + 8u), ok_color);
    cmos_write((u8)(PREF_BASE + 9u), err_color);
    cmos_write((u8)(PREF_BASE + 10u), text_color);
}

int persist_get_ui_prefs(
    u8* theme, u8* bg_preset, u8* animations, u8* high_contrast,
    u8* prompt_color, u8* input_color, u8* ok_color, u8* err_color, u8* text_color
) {
    if (!header_ok()) return -1;
    if (cmos_read((u8)(PREF_BASE + 0u)) != PREF_SIG0 ||
        cmos_read((u8)(PREF_BASE + 1u)) != PREF_SIG1) {
        return -1;
    }
    if (theme) *theme = cmos_read((u8)(PREF_BASE + 2u));
    if (bg_preset) *bg_preset = cmos_read((u8)(PREF_BASE + 3u));
    if (animations) *animations = cmos_read((u8)(PREF_BASE + 4u)) ? 1u : 0u;
    if (high_contrast) *high_contrast = cmos_read((u8)(PREF_BASE + 5u)) ? 1u : 0u;
    if (prompt_color) *prompt_color = cmos_read((u8)(PREF_BASE + 6u));
    if (input_color) *input_color = cmos_read((u8)(PREF_BASE + 7u));
    if (ok_color) *ok_color = cmos_read((u8)(PREF_BASE + 8u));
    if (err_color) *err_color = cmos_read((u8)(PREF_BASE + 9u));
    if (text_color) *text_color = cmos_read((u8)(PREF_BASE + 10u));
    return 0;
}
