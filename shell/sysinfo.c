#include "shell_sysinfo.h"
#include "kernel_api.h"

static void print_u32_local(u32 value) {
    char buf[16];
    int i = 0;

    if (value == 0) {
        kputc('0');
        return;
    }

    while (value > 0 && i < (int)sizeof(buf) - 1) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        kputc(buf[--i]);
    }
}

void shell_sysinfo_print(void) {
    kernel_metrics_t m;
    ksysinfo(&m);

    kputs("cores=");
    print_u32_local(m.core_count);
    kputs(" ticks=");
    print_u32_local(m.ticks);
    kputs(" sec_score=");
    print_u32_local(m.security_score);
    kputs(" threats=");
    print_u32_local(kthreat_count());
    kputc('\n');
}
