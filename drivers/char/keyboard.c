#include "keyboard.h"
#include "io.h"

#define KB_BUF_SIZE 128
#define KB_DATA_PORT 0x60u
#define KB_STATUS_PORT 0x64u
#define KB_STATUS_OUTPUT_FULL 0x01u
#define KB_STATUS_INPUT_FULL 0x02u
/* Bit 5 marks bytes produced by the auxiliary PS/2 device (mouse). */
#define KB_STATUS_AUX_OUTPUT 0x20u
#define KB_CTRL_READ_CONFIG 0x20u
#define KB_CTRL_WRITE_CONFIG 0x60u
#define KB_CTRL_ENABLE_FIRST_PORT 0xAEu
#define KB_DEVICE_ENABLE_SCANNING 0xF4u
#define KB_ACK 0xFAu

static char keybuf[KB_BUF_SIZE];
static u32 head = 0;
static u32 tail = 0;
static int shift_state = 0;
static int ctrl_state = 0;
static int alt_state = 0;
static u8 numlock_state = 1;
static u8 capslock_state = 0;
static u8 scrolllock_state = 0;
static u8 key_down[128];
static u8 ext_key_down[128];
static u32 irq_seen = 0;
static u8 e0_prefix = 0;

static int keyboard_wait_input_empty(void) {
    for (u32 spin = 0u; spin < 100000u; spin++) {
        if ((inb(KB_STATUS_PORT) & KB_STATUS_INPUT_FULL) == 0u) return 0;
    }
    return -1;
}

static int keyboard_wait_output_full(void) {
    for (u32 spin = 0u; spin < 100000u; spin++) {
        if (inb(KB_STATUS_PORT) & KB_STATUS_OUTPUT_FULL) return 0;
    }
    return -1;
}

static void keyboard_drain_controller(void) {
    for (u32 i = 0u; i < 32u && (inb(KB_STATUS_PORT) & KB_STATUS_OUTPUT_FULL); i++) {
        (void)inb(KB_DATA_PORT);
    }
}

/*
 * The keyboard and mouse share data port 0x60.  Never consume a mouse byte
 * from the keyboard path: doing so desynchronises mouse packets and can make
 * the next keyboard byte disappear on controllers that deliver both IRQs.
 */
static int keyboard_read_controller_byte(u8* out) {
    u8 status = inb(KB_STATUS_PORT);
    if ((status & KB_STATUS_OUTPUT_FULL) == 0u) return 0;
    if (status & KB_STATUS_AUX_OUTPUT) return 0;
    *out = inb(KB_DATA_PORT);
    return 1;
}

static void keyboard_enable_ps2_scanning(void) {
    u8 config;

    /* VirtualBox and real PS/2 controllers can retain BIOS state. Re-enable
       the first port and scan-code translation instead of relying on it. */
    keyboard_drain_controller();
    if (keyboard_wait_input_empty() != 0) return;
    outb(KB_STATUS_PORT, KB_CTRL_READ_CONFIG);
    if (keyboard_wait_output_full() != 0) return;
    config = inb(KB_DATA_PORT);
    config |= 0x01u;  /* First-port IRQ. */
    config |= 0x40u;  /* Translate scan-code set 2 to the set 1 map below. */
    config &= (u8)~0x10u; /* Ensure the first PS/2 clock is enabled. */

    if (keyboard_wait_input_empty() != 0) return;
    outb(KB_STATUS_PORT, KB_CTRL_WRITE_CONFIG);
    if (keyboard_wait_input_empty() != 0) return;
    outb(KB_DATA_PORT, config);
    if (keyboard_wait_input_empty() != 0) return;
    outb(KB_STATUS_PORT, KB_CTRL_ENABLE_FIRST_PORT);
    if (keyboard_wait_input_empty() != 0) return;
    outb(KB_DATA_PORT, KB_DEVICE_ENABLE_SCANNING);

    /* Consume the command ACK. It must never enter the shell's scan stream. */
    if (keyboard_wait_output_full() == 0) {
        u8 reply = inb(KB_DATA_PORT);
        if (reply != KB_ACK) {
            /* Ignore non-ACK controller replies; polling will still service input. */
        }
    }
}

static const char keymap[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x',
    'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0
};

static const char keymap_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X',
    'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0
};

void keyboard_init(void) {
    head = 0;
    tail = 0;
    shift_state = 0;
    ctrl_state = 0;
    alt_state = 0;
    irq_seen = 0;
    e0_prefix = 0;
    for (u32 i = 0; i < 128; i++) {
        key_down[i] = 0;
        ext_key_down[i] = 0;
    }
    keyboard_enable_ps2_scanning();
}

static int decode_scancode(u8 scancode, char* out_char) {
    if (scancode == 0xE0u) {
        e0_prefix = 1u;
        return 0;
    }

    if (e0_prefix) {
        u8 code = (u8)(scancode & 0x7Fu);
        e0_prefix = 0u;
        if (code < 128u) {
            ext_key_down[code] = (scancode & 0x80u) ? 0u : 1u;
        }
        if (code == 0x38u) {
            alt_state = (scancode & 0x80u) ? 0 : 1;
            return 0;
        }
        if (code == 0x1Du) {
            ctrl_state = (scancode & 0x80u) ? 0 : 1;
            return 0;
        }
        if (scancode & 0x80u) {
            return 0;
        }
        if (code == 0x48u) { *out_char = (char)KEY_UP; return 1; }      /* up */
        if (code == 0x50u) { *out_char = (char)KEY_DOWN; return 1; }    /* down */
        if (code == 0x4Bu) { *out_char = (char)(alt_state ? KEY_ALT_LEFT : KEY_LEFT); return 1; }  /* left */
        if (code == 0x4Du) { *out_char = (char)(alt_state ? KEY_ALT_RIGHT : KEY_RIGHT); return 1; } /* right */
        if (code == 0x47u) { *out_char = (char)KEY_HOME; return 1; }    /* home */
        if (code == 0x4Fu) { *out_char = (char)KEY_END; return 1; }     /* end */
        if (code == 0x49u) { *out_char = (char)KEY_PGUP; return 1; }    /* page up */
        if (code == 0x51u) { *out_char = (char)KEY_PGDN; return 1; }    /* page down */
        if (code == 0x52u) { *out_char = (char)KEY_INSERT; return 1; }  /* insert */
        if (code == 0x53u) { *out_char = (char)KEY_DELETE; return 1; }  /* delete */
        if (code == 0x1Cu) { *out_char = (char)'\n'; return 1; }        /* keypad enter */
        if (code == 0x35u) { *out_char = (char)'/'; return 1; }         /* keypad / */
        if (code == 0x5Bu || code == 0x5Cu) { *out_char = (char)KEY_SUPER; return 1; } /* Super / Windows key */
        return 0;
    }

    /* Emergency Rescue Hotkey: Ctrl + Alt + (Backspace: 0x0E or Esc: 0x01) */
    if (ctrl_state && alt_state) {
        if (scancode == 0x0Eu || scancode == 0x01u) {
            *out_char = (char)KEY_RESCUE;
            return 1;
        }
        if (scancode == 0x26u) { /* Ctrl+Alt+L -> Lock Screen */
            *out_char = (char)KEY_LOCK_SCREEN;
            return 1;
        }
    }

    u8 code = (u8)(scancode & 0x7Fu);

    if (scancode & 0x80u) {
        if (code < 128u) {
            key_down[code] = 0;
        }
        if (code == 0x2A || code == 0x36) {
            shift_state = 0;
        } else if (code == 0x1D) {
            ctrl_state = 0;
        } else if (code == 0x38) {
            alt_state = 0;
        }
        return 0;
    }

    if (scancode >= 128u) {
        return 0;
    }

    key_down[scancode] = 1;

    if (scancode == 0x2A || scancode == 0x36) {
        shift_state = 1;
        return 0;
    }

    if (scancode == 0x1D) {
        ctrl_state = 1;
        return 0;
    }

    if (scancode == 0x38) {
        alt_state = 1;
        return 0;
    }

    /* CapsLock Toggle */
    if (scancode == 0x3Au) {
        capslock_state = !capslock_state;
        *out_char = (char)KEY_CAPSLOCK_TOGGLE;
        return 1;
    }

    /* NumLock Toggle */
    if (scancode == 0x45u) {
        numlock_state = !numlock_state;
        *out_char = (char)KEY_NUMLOCK_TOGGLE;
        return 1;
    }

    /* ScrollLock Toggle */
    if (scancode == 0x46u) {
        scrolllock_state = !scrolllock_state;
        return 0;
    }

    /* ── Dedicated Numpad Scan Codes ── */
    /* Numpad 7 / Home */
    if (scancode == 0x47u) {
        *out_char = (numlock_state && !shift_state) ? '7' : (char)KEY_HOME;
        return 1;
    }
    /* Numpad 8 / Up */
    if (scancode == 0x48u) {
        *out_char = (numlock_state && !shift_state) ? '8' : (char)KEY_UP;
        return 1;
    }
    /* Numpad 9 / Page Up */
    if (scancode == 0x49u) {
        *out_char = (numlock_state && !shift_state) ? '9' : (char)KEY_PGUP;
        return 1;
    }
    /* Numpad - (Minus) */
    if (scancode == 0x4Au) {
        *out_char = '-';
        return 1;
    }
    /* Numpad 4 / Left */
    if (scancode == 0x4Bu) {
        *out_char = (numlock_state && !shift_state) ? '4' : (char)KEY_LEFT;
        return 1;
    }
    /* Numpad 5 */
    if (scancode == 0x4Cu) {
        *out_char = '5';
        return 1;
    }
    /* Numpad 6 / Right */
    if (scancode == 0x4Du) {
        *out_char = (numlock_state && !shift_state) ? '6' : (char)KEY_RIGHT;
        return 1;
    }
    /* Numpad + (Plus) */
    if (scancode == 0x4Eu) {
        *out_char = '+';
        return 1;
    }
    /* Numpad 1 / End */
    if (scancode == 0x4Fu) {
        *out_char = (numlock_state && !shift_state) ? '1' : (char)KEY_END;
        return 1;
    }
    /* Numpad 2 / Down */
    if (scancode == 0x50u) {
        *out_char = (numlock_state && !shift_state) ? '2' : (char)KEY_DOWN;
        return 1;
    }
    /* Numpad 3 / Page Down */
    if (scancode == 0x51u) {
        *out_char = (numlock_state && !shift_state) ? '3' : (char)KEY_PGDN;
        return 1;
    }
    /* Numpad 0 / Insert */
    if (scancode == 0x52u) {
        *out_char = (numlock_state && !shift_state) ? '0' : (char)KEY_INSERT;
        return 1;
    }
    /* Numpad . / Delete */
    if (scancode == 0x53u) {
        *out_char = (numlock_state && !shift_state) ? '.' : (char)KEY_DELETE;
        return 1;
    }
    /* Numpad * (Multiply) */
    if (scancode == 0x37u) {
        *out_char = '*';
        return 1;
    }

    /* Super / Windows Key (non-E0 scancode) */
    if (scancode == 0x5Bu || scancode == 0x5Cu) {
        *out_char = (char)KEY_SUPER;
        return 1;
    }

    /* F11 Key (Fullscreen Toggle) */
    if (scancode == 0x57u) {
        *out_char = (char)KEY_F11;
        return 1;
    }

    /* Alt+F2 Hotkey (Axiom Quick Run Command Dialog) */
    if (alt_state && scancode == 0x3Cu) {
        *out_char = (char)KEY_ALT_F2;
        return 1;
    }

    /* F3 Key -> macOS Mission Control / Exposé Window Overview */
    if (!alt_state && scancode == 0x3Du) {
        *out_char = (char)KEY_MISSION_CONTROL;
        return 1;
    }

    /* Ctrl+Alt+Left / Right -> Switch Workspaces */
    if (ctrl_state && alt_state && scancode == 0x4Bu) {
        *out_char = (char)KEY_WS_PREV;
        return 1;
    }
    if (ctrl_state && alt_state && scancode == 0x4Du) {
        *out_char = (char)KEY_WS_NEXT;
        return 1;
    }

    /* Ctrl+Alt+1..4 -> Direct jump to Workspace 1..4 */
    if (ctrl_state && alt_state && scancode >= 0x02u && scancode <= 0x05u) {
        *out_char = (char)(0xA1 + (scancode - 0x02u)); /* 0xA1..0xA4 -> Workspace 1..4 */
        return 1;
    }

    /* Alt+F4 Hotkey (Close Active Window) */
    if (alt_state && scancode == 0x3Eu) {
        *out_char = (char)KEY_ALT_F4;
        return 1;
    }

    /* Function Keys F1..F8 when Alt is held (Virtual Terminal switching) */
    if (alt_state && scancode >= 0x3Bu && scancode <= 0x42u) {
        *out_char = (char)(0x91 + (scancode - 0x3Bu)); /* 0x91 = Alt+F1 .. 0x98 = Alt+F8 */
        return 1;
    }

    char c = shift_state ? keymap_shift[scancode] : keymap[scancode];
    if (!c) {
        return 0;
    }

    /* CapsLock transforms alphabetic keys */
    if (capslock_state) {
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        } else if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
    }

    if (ctrl_state) {
        if (c == 'v' || c == 'V') {
            c = 22; /* Ctrl+V */
        } else if (c == 'c' || c == 'C') {
            c = 3;  /* Ctrl+C */
        } else if (c == 's' || c == 'S') {
            c = 19; /* Ctrl+S */
        } else if (c == 'x' || c == 'X') {
            c = 24; /* Ctrl+X */
        } else if (c == 'l' || c == 'L') {
            *out_char = (char)KEY_LOCK_SCREEN; /* Ctrl+L / Super+L Lock Screen */
            return 1;
        } else if (alt_state && (c == 't' || c == 'T')) {
            c = 20; /* Ctrl+Alt+T */
        }
    }

    if (alt_state && c == '\t') {
        *out_char = (char)KEY_ALT_TAB; /* Alt+Tab: desktop window switch */
        return 1;
    }

    *out_char = c;
    return 1;
}

static void push_char(char c) {
    u32 next = (head + 1) % KB_BUF_SIZE;
    if (next != tail) {
        keybuf[head] = c;
        head = next;
    }
}

void keyboard_inject_char(char c) {
    if (c) push_char(c);
}

void keyboard_on_irq(void* regs) {
    char c;
    u8 scancode;
    (void)regs;
    irq_seen++;
    while (keyboard_read_controller_byte(&scancode)) {
        c = 0;
        if (decode_scancode(scancode, &c)) {
            push_char(c);
        }
    }
}

static int poll_scancode_char(char* out) {
    u8 scancode;
    if (!keyboard_read_controller_byte(&scancode)) return 0;
    return decode_scancode(scancode, out);
}

int keyboard_read_char(void) {
    char c = 0;
    for (;;) {
        if (head != tail) {
            c = keybuf[tail];
            tail = (tail + 1) % KB_BUF_SIZE;
            return (int)(unsigned char)c;
        }
        if (poll_scancode_char(&c)) {
            return (int)(unsigned char)c;
        }
#if defined(__x86_64__) || defined(__i386__)
        __asm__ __volatile__("hlt");
#elif defined(__aarch64__)
        __asm__ __volatile__("wfi");
#endif
    }

    /* unreachable */
    return -1;
}

int keyboard_try_read_char(void) {
    char c = 0;
    if (head != tail) {
        c = keybuf[tail];
        tail = (tail + 1) % KB_BUF_SIZE;
        return (int)(unsigned char)c;
    }
    if (poll_scancode_char(&c)) {
        return (int)(unsigned char)c;
    }
    return -1;
}

int keyboard_is_scancode_down(u8 scancode) {
    if (scancode < 128u) return (int)key_down[scancode];
    return 0;
}

int keyboard_is_ext_scancode_down(u8 scancode) {
    if (scancode < 128u) return (int)ext_key_down[scancode];
    return 0;
}

int keyboard_get_numlock(void) {
    return (int)numlock_state;
}

int keyboard_get_capslock(void) {
    return (int)capslock_state;
}

void keyboard_set_numlock(int enabled) {
    numlock_state = enabled ? 1u : 0u;
}

void keyboard_set_capslock(int enabled) {
    capslock_state = enabled ? 1u : 0u;
}


