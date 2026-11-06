#include "task.h"
#include "string.h"
#include "frame_allocator.h"
#include "paging.h"
#include "serial.h"
#include "process_monitor.h"

#define MAX_TASKS 512

static task_struct_t s_tasks[MAX_TASKS];
static u32 s_task_count = 0;
static task_struct_t* s_current_task = 0;
static pid_t s_next_pid = 1;

extern void kthread_entry_stub(void);
extern void cpu_switch_to(task_struct_t* prev, task_struct_t* next);
extern void scheduler_schedule(void);

void task_init(void) {
    memset(s_tasks, 0, sizeof(s_tasks));

    /* Initialize Task 0 (Idle / Main Kernel Task) */
    task_struct_t* idle = &s_tasks[0];
    idle->pid = 0;
    idle->ppid = 0;
    idle->pgid = 0;
    idle->sid = 0;
    str_lcpy(idle->comm, "idle/swapper", MAX_COMM_LEN);
    idle->state = TASK_STATE_RUNNING;
    idle->flags = TASK_FLAG_KTHREAD | TASK_FLAG_IDLE;
    idle->priority = 39; /* lowest priority for idle */
    idle->policy = 3;   /* SCHED_POLICY_IDLE */
    idle->address_space = PAGING_SPACE_KERNEL;
    idle->home_core = 0;
    idle->has_context = 1;

    s_task_count = 1;
    s_current_task = idle;

    serial_writeln("[Task] Monolithic task subsystem initialized with PID 0 (idle/swapper)");
}

task_struct_t* task_get_current(void) {
    if (!s_current_task) {
        return &s_tasks[0];
    }
    return s_current_task;
}

void task_set_current(task_struct_t* task) {
    if (task) {
        s_current_task = task;
    }
}

task_struct_t* task_find_by_pid(pid_t pid) {
    for (u32 i = 0; i < MAX_TASKS; i++) {
        if (s_tasks[i].state != TASK_STATE_DEAD && s_tasks[i].state != TASK_STATE_EMBRYO) {
            if (s_tasks[i].pid == pid) {
                return &s_tasks[i];
            }
        }
    }
    return 0;
}

task_struct_t* task_alloc(void) {
    for (u32 i = 1; i < MAX_TASKS; i++) {
        if (s_tasks[i].state == TASK_STATE_DEAD || s_tasks[i].state == TASK_STATE_EMBRYO) {
            task_struct_t* t = &s_tasks[i];
            memset(t, 0, sizeof(task_struct_t));

            /* Allocate dedicated 16KB kernel stack (order 2 = 4 pages) */
            void* pphys = frame_alloc_order(2);
            if (!pphys) {
                serial_writeln("[Task] ERROR: Out of physical memory for kernel stack!");
                return 0;
            }
            void* pkstack = (void*)paging_phys_to_virt((u64)(uintptr_t)pphys);
            memset(pkstack, 0, KERNEL_STACK_SIZE);

            t->kernel_stack = pkstack;
            t->kernel_stack_top = (void*)((uintptr_t)pkstack + KERNEL_STACK_SIZE);
            t->pid = s_next_pid++;
            t->ppid = s_current_task ? s_current_task->pid : 0;
            t->state = TASK_STATE_EMBRYO;
            t->priority = 20; /* standard default priority */
            t->policy = 0;   /* SCHED_POLICY_CFS */
            t->address_space = PAGING_SPACE_KERNEL;

            if (i >= s_task_count) {
                s_task_count = i + 1;
            }
            return t;
        }
    }
    return 0;
}

void task_free(task_struct_t* task) {
    if (!task || task->pid == 0) return;

    if (task->kernel_stack) {
        u64 phys = paging_virt_to_phys((u64)(uintptr_t)task->kernel_stack);
        frame_free_order((void*)(uintptr_t)phys, 2);
        task->kernel_stack = 0;
        task->kernel_stack_top = 0;
    }
    task->state = TASK_STATE_DEAD;
}

int task_create_kthread(int (*fn)(void*), void* arg, const char* name, task_struct_t** out_task) {
    if (!fn) return -1;

    task_struct_t* t = task_alloc();
    if (!t) return -1;

    if (name) {
        str_lcpy(t->comm, name, MAX_COMM_LEN);
    } else {
        str_lcpy(t->comm, "kthread", MAX_COMM_LEN);
    }

    t->flags = TASK_FLAG_KTHREAD;

    /* Build initial context switch frame on the top of kernel stack
     * Stack layout popped by cpu_switch_to:
     * [rsp + 56]: kthread_entry_stub (return RIP)
     * [rsp + 48]: RBP = 0
     * [rsp + 40]: RBX = 0
     * [rsp + 32]: R12 = fn (thread function pointer)
     * [rsp + 24]: R13 = arg (thread argument pointer)
     * [rsp + 16]: R14 = 0
     * [rsp + 8] : R15 = 0
     * [rsp + 0] : RFLAGS = 0x202 (interrupts enabled)
     */
    u64* sp = (u64*)t->kernel_stack_top;
    sp--; *sp = (u64)(uintptr_t)kthread_entry_stub; /* Return address */
    sp--; *sp = 0;                                  /* RBP */
    sp--; *sp = 0;                                  /* RBX */
    sp--; *sp = (u64)(uintptr_t)fn;                 /* R12 */
    sp--; *sp = (u64)(uintptr_t)arg;                /* R13 */
    sp--; *sp = 0;                                  /* R14 */
    sp--; *sp = 0;                                  /* R15 */
    sp--; *sp = 0x202u;                             /* RFLAGS (IF=1) */

    t->context.rsp = (u64)(uintptr_t)sp;
    t->context.rip = (u64)(uintptr_t)kthread_entry_stub;
    t->context.cs = 0x08;
    t->context.ss = 0x10;
    t->has_context = 1;
    t->state = TASK_STATE_READY;

    process_monitor_register_named((u32)t->pid, t->comm);

    serial_write("[Task] Created kthread PID=");
    serial_write_dec((u32)t->pid);
    serial_write(" (");
    serial_write(t->comm);
    serial_writeln(")");

    if (out_task) {
        *out_task = t;
    }
    return 0;
}

void task_exit(int exit_code) {
    task_struct_t* cur = task_get_current();
    if (!cur || cur->pid == 0) {
        /* Idle task never exits */
        while (1) {
            __asm__ __volatile__("hlt");
        }
    }

    cur->exit_code = exit_code;
    cur->state = TASK_STATE_ZOMBIE;
    cur->flags |= TASK_FLAG_EXITING;

    serial_write("[Task] Process PID=");
    serial_write_dec((u32)cur->pid);
    serial_write(" exited with status ");
    serial_write_dec((u32)exit_code);
    serial_writeln("");

    /* Schedule next runnable task */
    scheduler_schedule();

    /* Should never reach here */
    while (1) {
        __asm__ __volatile__("hlt");
    }
}

void task_yield(void) {
    scheduler_schedule();
}
