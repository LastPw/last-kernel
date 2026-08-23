#ifndef KTHREAD_H
#define KTHREAD_H

#include "types.h"
#include "task.h"

typedef int (*kthread_fn_t)(void* data);

/* Kernel Thread Lifecycle Management */
task_struct_t* kthread_create(kthread_fn_t fn, void* data, const char* name);
int kthread_run(kthread_fn_t fn, void* data, const char* name);
void kthread_stop(task_struct_t* k);
int kthread_should_stop(void);

#endif /* KTHREAD_H */
