#include "shell.h"
#include "kernel_api.h"
#include "userdb.h"
#include "commands.h"
#include "bootinfo.h"
#include "scheduler.h"
#include "string.h"

#define LINE_MAX 128
#define SHELL_HIST_MAX 32
#define KEY_UP 0x81
#define KEY_DOWN 0x82
#define KEY_LEFT 0x83
#define KEY_RIGHT 0x84
#define KEY_HOME 0x85
#define KEY_END 0x86

static char g_user[24];
static char g_hostname[32] = "axiom";
static u8 g_prompt_color = 0x07;
static u8 g_input_color = 0x07;
static u8 g_ok_color = 0x07;
static u8 g_err_color = 0x07;
static u8 g_text_color = 0x07;

static void persist_shell_prefs(void) {
    u8 t=0,b=0,a=1,h=0,pc=0x07,ic=0x07,oc=0x07,ec=0x07,tc=0x07;
    (void)kpersist_get_ui_prefs(&t, &b, &a, &h, &pc, &ic, &oc, &ec, &tc);
    kpersist_set_ui_prefs(t, b, a, h, g_prompt_color, g_input_color, g_ok_color, g_err_color, g_text_color);
}
static char g_hist[SHELL_HIST_MAX][LINE_MAX];
static u32 g_hist_head = 0;
static u32 g_hist_count = 0;

static void hist_push(const char* line) {
    if (!line || !line[0]) return;
    str_ncpy(g_hist[g_hist_head], line, LINE_MAX - 1u);
    g_hist[g_hist_head][LINE_MAX - 1u] = '\0';
    g_hist_head = (g_hist_head + 1u) % SHELL_HIST_MAX;
    if (g_hist_count < SHELL_HIST_MAX) g_hist_count++;
}

static void load_persistent_shell_history(void) {
    char data[4096];
    int n = kvfs_read_file("/var/log/lastos.history", data, sizeof(data) - 1u);
    if (n <= 0) return;
    data[n] = '\0';
    u32 start = 0u;
    for (u32 i = 0u; i <= (u32)n; i++) {
        if (data[i] == '\n' || data[i] == '\0') {
            if (i > start) {
                data[i] = '\0';
                hist_push(data + start);
            }
            start = i + 1u;
        }
    }
}

static void clear_input_visual(u32 line_len, u32 cursor) {
    while (cursor < line_len) {
        kputc(' ');
        cursor++;
    }
    for (u32 i = 0u; i < line_len; i++) kputc('\b');
}

static void draw_input_at_cursor(const char* line, u32 len, u32 cursor, int password_mode) {
    for (u32 i = 0u; i < len; i++) kputc(password_mode ? '*' : line[i]);
    for (u32 i = cursor; i < len; i++) kputc('\b');
}

static u8 color_from_name(const char* name) {
    if (str_cmp(name, "black") == 0) return 0x00;
    if (str_cmp(name, "blue") == 0) return 0x09;
    if (str_cmp(name, "green") == 0) return 0x0A;
    if (str_cmp(name, "cyan") == 0) return 0x0B;
    if (str_cmp(name, "red") == 0) return 0x0C;
    if (str_cmp(name, "magenta") == 0) return 0x0D;
    if (str_cmp(name, "yellow") == 0) return 0x0E;
    if (str_cmp(name, "white") == 0) return 0x0F;
    if (str_cmp(name, "gray") == 0) return 0x07;
    return 0xFF;
}

static const char* skip_ws(const char* s) {
    while (s && (*s == ' ' || *s == '\t')) s++;
    return s ? s : "";
}

static void next_token(const char** inout, char* out, u32 out_sz) {
    u32 i = 0;
    const char* s = skip_ws(*inout);
    while (s[i] && s[i] != ' ' && s[i] != '\t' && i + 1u < out_sz) {
        out[i] = s[i];
        i++;
    }
    out[i] = '\0';
    *inout = s + i;
}

static void read_line(char* out, u32 max, int password_mode) {
    u32 i = 0;
    u32 cursor = 0;
    i32 browse = -1;

    for (;;) {
        int ch = kgetc();

        if (ch == 27) {
            out[0] = '\0';
            kputc('\n');
            break;
        }

        if (ch == '\n') {
            kputc('\n');
            break;
        }

        if (ch == '\b') {
            if (!password_mode && cursor > 0u) {
                u32 old_len = i;
                for (u32 j = cursor - 1u; j + 1u < i; j++) out[j] = out[j + 1u];
                i--;
                cursor--;
                clear_input_visual(old_len, cursor);
                draw_input_at_cursor(out, i, cursor, password_mode);
            } else if (password_mode && i > 0u) {
                i--; cursor = i; kputc('\b');
            }
            continue;
        }

        if (password_mode && ch == '\t') {
            continue;
        }

        if (!password_mode && (ch == KEY_UP || ch == KEY_DOWN)) {
            if (g_hist_count == 0u) {
                continue;
            }
            if (ch == KEY_UP) {
                if (browse + 1 < (i32)g_hist_count) browse++;
            } else {
                browse--;
                if (browse < -1) browse = -1;
            }

            if (browse >= 0) {
                u32 idx = (g_hist_head + SHELL_HIST_MAX - 1u - (u32)browse) % SHELL_HIST_MAX;
                str_ncpy(out, g_hist[idx], max - 1u);
                out[max - 1u] = '\0';
            } else {
                out[0] = '\0';
            }
            u32 old_len = i;
            i = str_len(out);
            if (i >= max) i = max - 1u;
            out[i] = '\0';
            clear_input_visual(old_len, cursor);
            draw_input_at_cursor(out, i, i, password_mode);
            cursor = i;
            continue;
        }

        if (!password_mode && ch == KEY_LEFT) {
            if (cursor > 0u) { kputc('\b'); cursor--; }
            continue;
        }
        if (!password_mode && ch == KEY_RIGHT) {
            if (cursor < i) { kputc(out[cursor]); cursor++; }
            continue;
        }
        if (!password_mode && ch == KEY_HOME) {
            while (cursor > 0u) { kputc('\b'); cursor--; }
            continue;
        }
        if (!password_mode && ch == KEY_END) {
            while (cursor < i) { kputc(out[cursor]); cursor++; }
            continue;
        }

        if (ch == '\t') {
            out[i] = '\0';
            const char* s = commands_suggest(out);
            if (s && str_ncmp(s, out, str_len(out)) == 0) {
                u32 j = str_len(out);
                while (s[j] && i + 1 < max) {
                    out[i++] = s[j];
                    kputc(s[j]);
                    j++;
                }
                cursor = i;
            }
            continue;
        }

        if (ch == 22) { /* Ctrl+V */
            char clip[128];
            kclip_get(clip, sizeof(clip));
            for (u32 j = 0; clip[j] && i + 1 < max; j++) {
                if (clip[j] < 32 || clip[j] > 126) {
                    continue;
                }
                if (cursor < i) {
                    for (u32 k = i; k > cursor; k--) out[k] = out[k - 1u];
                }
                out[cursor++] = clip[j];
                i++;
                kputc(clip[j]);
            }
            continue;
        }

        if (ch == 3) { /* Ctrl+C */
            clear_input_visual(i, cursor);
            i = 0; cursor = 0;
            continue;
        }

        if (ch >= 32 && ch <= 126 && i + 1 < max) {
            if (cursor < i) {
                for (u32 j = i; j > cursor; j--) out[j] = out[j - 1u];
            }
            out[cursor++] = (char)ch;
            i++;
            if (password_mode) kputc('*');
            else {
                clear_input_visual(i - 1u, cursor - 1u);
                draw_input_at_cursor(out, i, cursor, password_mode);
            }
        }
    }

    out[i] = '\0';
}

static void shell_login(void) {
    char user[24];
    char pass[24];
    char pass2[24];
    char hostname[32];
    char stored_user[24];

    if (!kpersist_auth_exists()) {
        kclear();
        for (;;) {
            ksetcolor(g_text_color);
            kputs("First boot setup\n");
            kputs("Create username: ");
            ksetcolor(g_input_color);
            read_line(user, sizeof(user), 0);
            if (!user[0]) {
                ksetcolor(g_err_color);
                kputs("Username cannot be empty.\n");
                ksetcolor(g_text_color);
                continue;
            }
            ksetcolor(g_text_color);
            kputs("Create password: ");
            ksetcolor(g_input_color);
            read_line(pass, sizeof(pass), 1);
            if (!pass[0]) {
                ksetcolor(g_err_color);
                kputs("Password cannot be empty.\n");
                continue;
            }
            ksetcolor(g_text_color);
            kputs("Confirm password: ");
            ksetcolor(g_input_color);
            read_line(pass2, sizeof(pass2), 1);
            if (str_cmp(pass, pass2) != 0) {
                ksetcolor(g_err_color);
                kputs("Password mismatch.\n");
                continue;
            }
            kpersist_set_auth(user, pass);
            (void)userdb_sync_primary();
            ksetcolor(g_text_color);
            kputs("Computer hostname [axiom]: ");
            ksetcolor(g_input_color);
            read_line(hostname, sizeof(hostname), 0);
            if (!hostname[0]) str_cpy(hostname, "axiom");
            kpersist_set_hostname(hostname);
            str_ncpy(g_hostname, hostname, sizeof(g_hostname) - 1u);
            g_hostname[sizeof(g_hostname) - 1u] = '\0';
            (void)commands_env_set("HOSTNAME", g_hostname);
            ksetcolor(g_ok_color);
            kputs("Administrator identity saved.\n");
            ksetcolor(g_text_color);
            break;
        }
    }

    if (kpersist_get_auth(stored_user, sizeof(stored_user), 0, 0u) != 0) {
        ksetcolor(g_err_color);
        kputs("Auth storage error.\n");
        return;
    }

    for (;;) {

        /* Graphics login cancelled or unavailable — text fallback. */
        ksetcolor(g_text_color);
        kputs("Login required\n");
        kputs("Username: ");
        ksetcolor(g_input_color);
        read_line(user, sizeof(user), 0);

        ksetcolor(g_text_color);
        kputs("Password: ");
        ksetcolor(g_input_color);
        read_line(pass, sizeof(pass), 1);

        if (userdb_login(user, pass) == 0 ||
            (str_cmp(user, stored_user) == 0 && kpersist_auth_verify(user, pass) == 0)) {
            str_ncpy(g_user, user, sizeof(g_user) - 1u);
            g_user[sizeof(g_user) - 1u] = '\0';
            (void)commands_env_set("USER", g_user);
            (void)commands_env_set("HOSTNAME", g_hostname);
            ksetcolor(g_ok_color);
            kputs("Login success.\n");
            ksetcolor(g_text_color);
            return;
        }

        ksetcolor(g_err_color);
        kputs("Invalid username or password.\n");
        ksetcolor(g_text_color);
    }
}

static u32 shell_line_capacity(void) {
    u32 cols = kconsole_cols();
    u32 prompt_len = str_len(g_user[0] ? g_user : "user") +
                     str_len(g_hostname[0] ? g_hostname : "axiom") +
                     str_len(commands_cwd()) + 5u; /* user@host:C:/path$ */
    u32 cap = (cols > prompt_len + 2u) ? (cols - prompt_len - 1u) : 16u;
    if (cap > (LINE_MAX - 1u)) cap = LINE_MAX - 1u;
    if (cap < 16u) cap = 16u;
    return cap;
}

static int handle_set_color(const char* line) {
    const char* p = line;
    char t1[16], t2[16], t3[16], t4[16];
    u8 c;

    next_token(&p, t1, sizeof(t1));
    next_token(&p, t2, sizeof(t2));
    if (str_cmp(t1, "set") != 0 || str_cmp(t2, "color") != 0) {
        return 0;
    }

    next_token(&p, t3, sizeof(t3));
    if (!t3[0]) {
        ksetcolor(g_text_color);
        kputs("Usage: set color <name>\n");
        kputs("Usage: set color <prompt|input|success|error|text> <name>\n");
        return 1;
    }

    next_token(&p, t4, sizeof(t4));
    if (!t4[0]) {
        c = color_from_name(t3);
        if (c == 0xFF) {
            ksetcolor(g_err_color);
            kputs("Unknown color.\n");
            return 1;
        }
        g_prompt_color = c;
        g_input_color = c;
        g_text_color = c;
        persist_shell_prefs();
        ksetcolor(g_ok_color);
        kputs("Color updated.\n");
        return 1;
    }

    c = color_from_name(t4);
    if (c == 0xFF) {
        ksetcolor(g_err_color);
        kputs("Unknown color.\n");
        return 1;
    }

    if (str_cmp(t3, "prompt") == 0) g_prompt_color = c;
    else if (str_cmp(t3, "input") == 0) g_input_color = c;
    else if (str_cmp(t3, "success") == 0) g_ok_color = c;
    else if (str_cmp(t3, "error") == 0) g_err_color = c;
    else if (str_cmp(t3, "text") == 0) g_text_color = c;
    else {
        ksetcolor(g_err_color);
        kputs("Unknown target.\n");
        return 1;
    }

    persist_shell_prefs();
    ksetcolor(g_ok_color);
    kputs("Color target updated.\n");
    return 1;
}

static void print_prompt(int last_ok) {
    const char* cwd = commands_cwd();
    if (last_ok) ksetcolor(g_ok_color);
    else ksetcolor(g_err_color);
    if (!g_user[0]) str_ncpy(g_user, "user", sizeof(g_user) - 1u);
    kputs(g_user);
    kputc('@');
    kputs(g_hostname[0] ? g_hostname : "axiom");
    kputs(":C:");
    kputs(cwd && cwd[0] == '/' ? cwd : "/");
    kputs("$ ");
    ksetcolor(g_input_color);
}

static void print_banner(void) {
    ksetcolor(g_text_color);
    kputs("Axiom Windows [Version 0.1.0]\n");
    kputs("(c) Axiom Corporation. All rights reserved.\n\n");
}

void shell_init(void) {
    g_user[0] = '\0';
    commands_env_init();
    commands_history_init();
    load_persistent_shell_history();
    if (kpersist_get_hostname(g_hostname, sizeof(g_hostname)) != 0) {
        str_cpy(g_hostname, "axiom");
    }
    {
        u8 t=0,b=0,a=1,h=0,pc=0x07,ic=0x07,oc=0x07,ec=0x07,tc=0x07;
        (void)kpersist_get_ui_prefs(&t, &b, &a, &h, &pc, &ic, &oc, &ec, &tc);
        /* Keep the command-line theme consistent with the Axiom terminal. */
        g_prompt_color = 0x07;
        g_input_color = 0x07;
        g_ok_color = 0x07;
        g_err_color = 0x07;
        g_text_color = 0x07;
    }
}

static void shell_command_loop(void) {
    char line[LINE_MAX];
    char expanded[192];
    int rc = 1;

    for (;;) {
        print_prompt(rc);
        read_line(line, shell_line_capacity() + 1u, 0);

        if (handle_set_color(line)) {
            rc = 1;
            continue;
        }

        ksetcolor(g_text_color);
        hist_push(line);
        (void)commands_env_set("USER", g_user);
        (void)commands_env_set("HOSTNAME", g_hostname);
        (void)commands_env_set("PWD", commands_cwd());
        (void)commands_env_expand(line, expanded, sizeof(expanded));
        rc = commands_execute_status(expanded);
    }
}

void shell_run(void) {
    print_banner();
    shell_login();
    shell_command_loop();
}

void shell_resume(void) {
    scheduler_begin_kernel_resume();
    shell_command_loop();
}
