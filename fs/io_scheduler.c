#include "io_scheduler.h"
#include "cgroup.h"
#include "resource_engine.h"
#include "security_hook.h"

static io_stats_t st;
static u32 last_tick = 0;

void io_scheduler_init(void) {
    st.queue_depth = 0;
    st.ops_total = 0;
    st.bytes_total = 0;
    st.dropped_ops = 0;
    st.net_packets = 0;
    st.mode = IO_MODE_ADAPTIVE;
    last_tick = 0;
}

void io_scheduler_set_mode(io_mode_t mode) {
    st.mode = mode;
}

io_mode_t io_scheduler_mode(void) {
    return st.mode;
}

int io_scheduler_submit(u32 task_id, u32 bytes, int is_read) {
    (void)is_read;
    u32 gid = cgroup_task_group(task_id);
    cgroup_info_t info;
    u32 io_limit = 0;
    for (u32 i = 0; i < cgroup_count(); i++) {
        if (cgroup_get_by_index(i, &info) == 0 && info.id == gid) {
            io_limit = info.io_limit_ops;
            break;
        }
    }
    if (io_limit && st.queue_depth >= io_limit) {
        st.dropped_ops++;
        return -1;
    }

    st.queue_depth++;
    st.ops_total++;
    st.bytes_total += bytes;
    resource_engine_note_io(task_id, bytes, 0);
    security_hook_notify(HOOK_EVT_IO, task_id, bytes);
    return 0;
}

void io_scheduler_tick(u32 now_tick) {
    if (last_tick == 0) {
        last_tick = now_tick;
    }

    u32 release = 1;
    if (st.mode == IO_MODE_THROUGHPUT) {
        release = 4;
    } else if (st.mode == IO_MODE_ADAPTIVE) {
        release = (st.queue_depth > 16) ? 4 : 2;
    }

    while (release > 0 && st.queue_depth > 0) {
        st.queue_depth--;
        release--;
    }
    last_tick = now_tick;
}

void io_scheduler_stats(io_stats_t* out) {
    if (!out) {
        return;
    }
    *out = st;
}

void io_scheduler_account_net_packet(void) {
    st.net_packets++;
    resource_engine_note_io(0, 64, 1);
}
