#include "shell_threatlog.h"
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

void shell_threatlog_print(void) {
    u32 n = kthreat_count();
    if (n == 0) {
        kputs("threatlog: empty\n");
        return;
    }

    u32 limit = (n < 10) ? n : 10;
    for (u32 i = 0; i < limit; i++) {
        threat_record_t r;
        if (kthreat_get(i, &r) != 0) {
            continue;
        }   

        kputs("#");
        print_u32_local(r.id);
        kputs(" t=");
        print_u32_local(r.tick);
        kputs(" task=");
        print_u32_local(r.task_id);
        kputs(" type=");
        print_u32_local(r.type);
        kputs(" a0=");
        print_u32_local(r.arg0);
        kputs(" a1=");
        print_u32_local(r.arg1);
        kputc('\n');
    }
}
