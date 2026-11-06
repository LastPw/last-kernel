#include "kernel_api.h"
#include "serial.h"
#include "entropy.h"

/* Initial compile-time placeholder — overwritten by stack_protector_init()
   using entropy_fill() before any canary-checked function executes. */
u32 __stack_chk_guard = 0x0A7105C7u;

/* Called very early from kmain() to randomize the canary.  Must not have
   a stack protector check itself since we are changing the guard while
   this function's own frame is live. */
__attribute__((no_stack_protector))
void stack_protector_init(void) {
    u8 entropy[4];
    entropy_fill(entropy, sizeof(entropy));
    __stack_chk_guard = ((u32)entropy[0] << 0u) | ((u32)entropy[1] << 8u) |
                        ((u32)entropy[2] << 16u) | ((u32)entropy[3] << 24u);
    /* Ensure the canary is never zero — zero canaries defeat the check. */
    if (__stack_chk_guard == 0u) __stack_chk_guard = 0xDEADBEEFu;
    serial_writeln("[LastOS] stack canary randomized");
}

void __stack_chk_fail(void) {
    panic("stack canary failure");
    for (;;) { __asm__ __volatile__("cli; hlt"); }
}

void __stack_chk_fail_local(void) { __stack_chk_fail(); }
