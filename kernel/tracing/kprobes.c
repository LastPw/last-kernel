#include "kprobes.h"
#include "serial.h"
#include "string.h"

static kprobe_t g_kprobes[16];
static u32 g_kprobe_count = 0;

int kprobes_init(void) {
    memset(g_kprobes, 0, sizeof(g_kprobes));
    g_kprobe_count = 0;
    serial_writeln("[KPROBES] Kernel Dynamic Instruction Breakpoint Tracer Initialized");
    return 0;
}

int register_kprobe(u64 addr, kprobe_pre_handler_t pre, kprobe_post_handler_t post) {
    if (addr == 0 || g_kprobe_count >= 16) return -1;
    for (u32 i = 0; i < g_kprobe_count; i++) {
        if (g_kprobes[i].addr == addr && g_kprobes[i].active) return -1;
    }
    kprobe_t* kp = &g_kprobes[g_kprobe_count++];
    kp->addr = addr;
    kp->pre_handler = pre;
    kp->post_handler = post;
    kp->active = 1;
    serial_writeln("[KPROBES] Registered dynamic probe at target kernel address");
    return 0;
}

int unregister_kprobe(u64 addr) {
    for (u32 i = 0; i < g_kprobe_count; i++) {
        if (g_kprobes[i].addr == addr) {
            g_kprobes[i].active = 0;
            return 0;
        }
    }
    return -1;
}
