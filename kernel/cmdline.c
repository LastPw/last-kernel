#include "cmdline.h"
#include "string.h"
#include "serial.h"

/*
 * Gentoo-Tier Kernel Boot Command-Line Argument Parser
 *
 * Tokenizes kernel boot parameters passed via bootloader (e.g. Limine, GRUB)
 * into structured key-value pairs for granular kernel tuning.
 */

static char          s_raw_cmdline[MAX_CMDLINE_LEN];
static cmdline_arg_t s_args[MAX_CMDLINE_ARGS];
static u32           s_arg_count = 0;
static u8            s_cmdline_inited = 0;

void cmdline_init(const char* raw_str) {
    memset(s_raw_cmdline, 0, sizeof(s_raw_cmdline));
    memset(s_args, 0, sizeof(s_args));
    s_arg_count = 0;

    if (!raw_str || !*raw_str) {
        /* Default Hardcore Gentoo-Style Kernel Parameters */
        raw_str = "apk.morph=balanced pku=strict mitigations=off loglevel=info console=tty0";
    }

    str_ncpy(s_raw_cmdline, raw_str, MAX_CMDLINE_LEN - 1);

    const char* p = s_raw_cmdline;
    while (*p && s_arg_count < MAX_CMDLINE_ARGS) {
        while (*p == ' ') p++;
        if (!*p) break;

        const char* start = p;
        while (*p && *p != ' ') p++;
        u32 len = (u32)(p - start);

        /* Search for '=' inside the token */
        const char* eq = 0;
        for (u32 i = 0; i < len; i++) {
            if (start[i] == '=') {
                eq = start + i;
                break;
            }
        }

        if (eq) {
            u32 klen = (u32)(eq - start);
            u32 vlen = len - klen - 1;
            if (klen > 31) klen = 31;
            if (vlen > 63) vlen = 63;

            memcpy(s_args[s_arg_count].key, start, klen);
            s_args[s_arg_count].key[klen] = '\0';

            memcpy(s_args[s_arg_count].val, eq + 1, vlen);
            s_args[s_arg_count].val[vlen] = '\0';
            s_args[s_arg_count].has_val = 1;
        } else {
            u32 klen = len;
            if (klen > 31) klen = 31;
            memcpy(s_args[s_arg_count].key, start, klen);
            s_args[s_arg_count].key[klen] = '\0';
            s_args[s_arg_count].has_val = 0;
        }

        s_arg_count++;
    }

    s_cmdline_inited = 1;
    serial_write("[CMDLINE] Kernel Boot Parameters Parsed: ");
    serial_write(s_raw_cmdline);
    serial_write("\n");
}

int cmdline_has_flag(const char* key) {
    if (!s_cmdline_inited) cmdline_init(0);
    if (!key) return 0;

    for (u32 i = 0; i < s_arg_count; i++) {
        if (str_cmp(s_args[i].key, key) == 0) {
            if (!s_args[i].has_val) return 1;
            if (str_cmp(s_args[i].val, "1") == 0 ||
                str_cmp(s_args[i].val, "on") == 0 ||
                str_cmp(s_args[i].val, "yes") == 0 ||
                str_cmp(s_args[i].val, "true") == 0) {
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

const char* cmdline_get_val(const char* key) {
    if (!s_cmdline_inited) cmdline_init(0);
    if (!key) return 0;

    for (u32 i = 0; i < s_arg_count; i++) {
        if (str_cmp(s_args[i].key, key) == 0) {
            return s_args[i].has_val ? s_args[i].val : "";
        }
    }
    return 0;
}

i32 cmdline_get_int(const char* key, i32 default_val) {
    const char* val = cmdline_get_val(key);
    if (!val || !*val) return default_val;

    i32 res = 0;
    i32 sign = 1;
    if (*val == '-') {
        sign = -1;
        val++;
    }

    while (*val >= '0' && *val <= '9') {
        res = res * 10 + (*val - '0');
        val++;
    }
    return res * sign;
}

const char* cmdline_get_raw(void) {
    if (!s_cmdline_inited) cmdline_init(0);
    return s_raw_cmdline;
}

u32 cmdline_get_arg_count(void) {
    if (!s_cmdline_inited) cmdline_init(0);
    return s_arg_count;
}

void cmdline_dump(void) {
    if (!s_cmdline_inited) cmdline_init(0);
    serial_write("--- Kernel Cmdline Parameters ---\n");
    for (u32 i = 0; i < s_arg_count; i++) {
        serial_write("  [");
        serial_write(s_args[i].key);
        if (s_args[i].has_val) {
            serial_write(" = ");
            serial_write(s_args[i].val);
        }
        serial_write("]\n");
    }
}
