#include "kthread.h"
#include "task.h"
#include "string.h"
#include "serial.h"

task_struct_t* kthread_create(kthread_fn_t fn, void* data, const char* name) {
    task_struct_t* k = 0;
    int res = task_create_kthread(fn, data, name ? name : "kthread", &k);
    if (res != 0 || !k) {
        return 0;
    }
    return k;
}

int kthread_run(kthread_fn_t fn, void* data, const char* name) {
    task_struct_t* k = kthread_create(fn, data, name);
    if (!k) return -1;
    return (int)k->pid;
}

void kthread_stop(task_struct_t* k) {
    if (!k) return;
    k->flags |= TASK_FLAG_EXITING;
}

int kthread_should_stop(void) {
    task_struct_t* cur = task_get_current();
    if (!cur) return 0;
    return (cur->flags & TASK_FLAG_EXITING) != 0;
}
