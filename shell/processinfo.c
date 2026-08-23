#include "shell_processinfo.h"
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

void shell_processinfo_print(void) {
    u32 count = kprocess_count();
    if (count == 0) {
        kputs("processinfo: empty\n");
        return;
    }

    for (u32 i = 0; i < count; i++) {
        process_info_t p;
        if (kprocess_at(i, &p) != 0) {
            continue;
        }

        kputs("pid=");
        print_u32_local(p.task_id);
        kputs(" name=");
        kputs(p.name[0] ? p.name : "task");
        kputs(" ticks=");
        print_u32_local(p.runtime_ticks);
        kputs(" score=");
        print_u32_local(p.suspicious_score);
        kputs(" blocked=");
        print_u32_local(p.blocked ? 1 : 0);
        kputc('\n');
    }
}
