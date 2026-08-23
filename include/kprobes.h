#ifndef KPROBES_H
#define KPROBES_H

#include "types.h"

typedef void (*kprobe_pre_handler_t)(u64 addr, void* regs);
typedef void (*kprobe_post_handler_t)(u64 addr, void* regs);

typedef struct {
    u64 addr;
    u8 original_byte;
    u8 active;
    kprobe_pre_handler_t pre_handler;
    kprobe_post_handler_t post_handler;
} kprobe_t;

int kprobes_init(void);
int register_kprobe(u64 addr, kprobe_pre_handler_t pre, kprobe_post_handler_t post);
int unregister_kprobe(u64 addr);

#endif
