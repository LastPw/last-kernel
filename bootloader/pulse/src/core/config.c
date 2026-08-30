#include "config.h"
#include "lib.h"

#define MAX_KEY 32

static int is_space(char c)
{
    return c == ' ' || c == '\t';
}

static int parse_u32(const char *s, UINTN len, UINT32 *out)
{
    UINT64 v = 0;
    if (len == 0)
        return 0;
    for (UINTN i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
        v = v * 10 + (UINT64)(s[i] - '0');
        if (v > 0xFFFFFFFFULL)
            return 0;
    }
    *out = (UINT32)v;
    return 1;
}

static void store_capped(char *dst, UINTN cap, const char *src, UINTN len)
{
    if (len >= cap)
        len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

/* start a fresh entry, returning pointer or NULL when full */
static cfg_entry_t *open_entry(pulse_config_t *cfg)
{
    if (cfg->count >= PULSE_MAX_ENTRIES)
        return NULL;
    cfg_entry_t *e = &cfg->entry[cfg->count++];
    memset(e, 0, sizeof(*e));
    store_capped(e->title, sizeof(e->title), "entry", 5);
    return e;
}

INT32 config_parse(const char *data, UINTN len,
                   pulse_config_t *cfg, UINTN *err_line)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->default_idx = 0;

    /* 'cur' is opened lazily: v1 configs get an implicit first entry,
       section headers always begin a fresh one */
    cfg_entry_t *cur = 0;
    UINTN line = 0, i = 0;

    while (i <= len) {
        UINTN start = i;
        while (i < len && data[i] != '\n')
            i++;
        UINTN end = i;
        line++;

        if (end > start && data[end - 1] == '\r')
            end--;

        UINTN j;
        for (j = start; j < end; j++)
            if (data[j] == '#')
                break;
        UINTN ls = start, le = j;
        while (ls < le && is_space(data[ls]))
            ls++;
        while (le > ls && is_space(data[le - 1]))
            le--;

        if (i >= len)
            break;
        i++;

        if (ls == le)
            continue;

        /* section header: [entry name] */
        if (data[ls] == '[') {
            if (le <= ls + 2 || data[le - 1] != ']') {
                if (err_line) *err_line = line;
                return -1;
            }
            UINTN ts = ls + 1, te = le - 1;
            /* optional "entry " prefix */
            if (te - ts > 6 &&
                strncmp(data + ts, "entry ", 6) == 0)
                ts += 6;
            cur = open_entry(cfg);
            if (!cur) {
                if (err_line) *err_line = line;
                return -1;
            }
            while (ts < te && is_space(data[ts])) ts++;
            while (te > ts && is_space(data[te - 1])) te--;
            if (te > ts)
                store_capped(cur->title, sizeof(cur->title),
                             data + ts, te - ts);
            continue;
        }

        UINTN eq = ls;
        while (eq < le && data[eq] != '=')
            eq++;
        if (eq == le || eq == ls || eq + 1 == le) {
            if (err_line) *err_line = line;
            return -1;
        }

        UINTN ks = ls, ke = eq;
        while (ke > ks && is_space(data[ke - 1])) ke--;
        UINTN vs = eq + 1, ve = le;
        while (vs < ve && is_space(data[vs])) vs++;
        while (ve > vs && is_space(data[ve - 1])) ve--;
        if (ve - vs >= 2 && data[vs] == '"' && data[ve - 1] == '"') {
            vs++;
            ve--;
        }
        UINTN klen = ke - ks;
        const char *key = data + ks;

        if (klen == 7 && strncmp(key, "timeout", 7) == 0) {
            if (!parse_u32(data + vs, ve - vs, &cfg->timeout)) {
                if (err_line) *err_line = line;
                return -1;
            }
        } else if (klen == 7 && strncmp(key, "default", 7) == 0) {
            if (!parse_u32(data + vs, ve - vs, &cfg->default_idx)) {
                if (err_line) *err_line = line;
                return -1;
            }
        } else if (klen == 6 && strncmp(key, "kernel", 6) == 0) {
            if (!cur && !(cur = open_entry(cfg))) {
                if (err_line) *err_line = line;
                return -1;
            }
            if (ve == vs) {
                if (err_line) *err_line = line;
                return -1;
            }
            store_capped(cur->kernel, sizeof(cur->kernel),
                         data + vs, ve - vs);
        } else if (klen == 7 && strncmp(key, "cmdline", 7) == 0) {
            if (!cur && !(cur = open_entry(cfg))) {
                if (err_line) *err_line = line;
                return -1;
            }
            store_capped(cur->cmdline, sizeof(cur->cmdline),
                         data + vs, ve - vs);
        } else if (klen >= MAX_KEY) {
            if (err_line) *err_line = line;
            return -1;
        }
        /* unknown keys ignored */
    }

    /* drop trailing empty implicit entry (header-only configs) */
    if (cfg->count > 0 && cfg->entry[cfg->count - 1].kernel[0] == 0 &&
        cfg->count > 1)
        cfg->count--;

    /* validate entries */
    for (UINTN k = 0; k < cfg->count; k++)
        if (cfg->entry[k].kernel[0] == 0) {
            if (err_line) *err_line = line ? line : 1;
            return -2;
        }
    if (cfg->count == 0) {
        if (err_line) *err_line = line ? line : 1;
        return -2;
    }
    if (cfg->default_idx >= cfg->count)
        cfg->default_idx = 0;
    return 0;
}
