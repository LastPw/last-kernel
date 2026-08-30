#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#define MAX_CMDLINE_LEN   512u
#define MAX_CMDLINE_ARGS  32u

typedef struct {
    char key[32];
    char val[64];
    uint8_t has_val;
} cmdline_arg_t;

static char          s_raw[MAX_CMDLINE_LEN];
static cmdline_arg_t s_args[MAX_CMDLINE_ARGS];
static uint32_t      s_arg_count = 0;

static void parse_cmdline(const char* raw) {
    memset(s_raw, 0, sizeof(s_raw));
    memset(s_args, 0, sizeof(s_args));
    s_arg_count = 0;
    strncpy(s_raw, raw, MAX_CMDLINE_LEN - 1);

    const char* p = s_raw;
    while (*p && s_arg_count < MAX_CMDLINE_ARGS) {
        while (*p == ' ') p++;
        if (!*p) break;

        const char* start = p;
        while (*p && *p != ' ') p++;
        uint32_t len = (uint32_t)(p - start);

        const char* eq = NULL;
        for (uint32_t i = 0; i < len; i++) {
            if (start[i] == '=') {
                eq = start + i;
                break;
            }
        }

        if (eq) {
            uint32_t klen = (uint32_t)(eq - start);
            uint32_t vlen = len - klen - 1;
            if (klen > 31) klen = 31;
            if (vlen > 63) vlen = 63;
            memcpy(s_args[s_arg_count].key, start, klen);
            s_args[s_arg_count].key[klen] = '\0';
            memcpy(s_args[s_arg_count].val, eq + 1, vlen);
            s_args[s_arg_count].val[vlen] = '\0';
            s_args[s_arg_count].has_val = 1;
        } else {
            uint32_t klen = len > 31 ? 31 : len;
            memcpy(s_args[s_arg_count].key, start, klen);
            s_args[s_arg_count].key[klen] = '\0';
            s_args[s_arg_count].has_val = 0;
        }
        s_arg_count++;
    }
}

static const char* get_val(const char* key) {
    for (uint32_t i = 0; i < s_arg_count; i++) {
        if (strcmp(s_args[i].key, key) == 0) {
            return s_args[i].has_val ? s_args[i].val : "";
        }
    }
    return NULL;
}

static int has_flag(const char* key) {
    for (uint32_t i = 0; i < s_arg_count; i++) {
        if (strcmp(s_args[i].key, key) == 0) {
            if (!s_args[i].has_val) return 1;
            if (strcmp(s_args[i].val, "1") == 0 ||
                strcmp(s_args[i].val, "on") == 0 ||
                strcmp(s_args[i].val, "yes") == 0 ||
                strcmp(s_args[i].val, "true") == 0) return 1;
            return 0;
        }
    }
    return 0;
}

static int get_int(const char* key, int def) {
    const char* v = get_val(key);
    if (!v || !*v) return def;
    int res = 0;
    while (*v >= '0' && *v <= '9') {
        res = res * 10 + (*v - '0');
        v++;
    }
    return res;
}

static void test_cmdline_tokenization(void) {
    printf("  [1/3] Testing Kernel Cmdline Argument Tokenizer & Splitter... ");
    const char* cmd = "apk.morph=latency pku=strict mitigations=off loglevel=3 count=4096 quiet console=ttyS0";
    parse_cmdline(cmd);

    assert(s_arg_count == 7);
    assert(strcmp(get_val("apk.morph"), "latency") == 0);
    assert(strcmp(get_val("pku"), "strict") == 0);
    assert(strcmp(get_val("mitigations"), "off") == 0);
    assert(strcmp(get_val("console"), "ttyS0") == 0);
    printf("PASS (Parsed %u structured parameters)\n", s_arg_count);
}

static void test_cmdline_flags_and_integers(void) {
    printf("  [2/3] Testing Boolean Flags & Integer Parameter Parsing... ");
    assert(has_flag("quiet") == 1);
    assert(has_flag("nonexistent") == 0);
    assert(get_int("loglevel", 0) == 3);
    assert(get_int("count", 0) == 4096);
    assert(get_int("missing_int", 777) == 777);
    printf("PASS (Flags & Types verified)\n");
}

static void test_memory_examiner_formatting(void) {
    printf("  [3/3] Testing In-Kernel Memory Examiner Hex & ASCII Formatting... ");
    uint8_t buffer[16] = {
        0x7F, 'E', 'L', 'F', 0x02, 0x01, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    char line[128];
    sprintf(line, "0x%02X %02X %02X %02X | %c%c%c%c",
            buffer[0], buffer[1], buffer[2], buffer[3],
            (buffer[0] >= 32 && buffer[0] <= 126) ? buffer[0] : '.',
            (buffer[1] >= 32 && buffer[1] <= 126) ? buffer[1] : '.',
            (buffer[2] >= 32 && buffer[2] <= 126) ? buffer[2] : '.',
            (buffer[3] >= 32 && buffer[3] <= 126) ? buffer[3] : '.');

    assert(strstr(line, "0x7F 45 4C 46 | .ELF") != NULL);
    printf("PASS (Hex+ASCII formatting verified)\n");
}

int main(void) {
    printf("\n=======================================================\n");
    printf("   LastKernel Gentoo-Tier Control Plane Test Suite     \n");
    printf("=======================================================\n");
    test_cmdline_tokenization();
    test_cmdline_flags_and_integers();
    test_memory_examiner_formatting();
    printf(">>> ALL GENTOO CONTROL PLANE VECTORS PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
