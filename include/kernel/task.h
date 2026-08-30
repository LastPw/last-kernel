#ifndef TASK_H
#define TASK_H

#include "types.h"
#include "idt.h"

typedef i32 pid_t;

/* Standard POSIX-compatible Task States */
typedef enum {
    TASK_STATE_EMBRYO      = 0,
    TASK_STATE_READY       = 1,
    TASK_STATE_RUNNING     = 2,
    TASK_STATE_SLEEPING    = 3,
    TASK_STATE_BLOCKED     = 4,
    TASK_STATE_STOPPED     = 5,
    TASK_STATE_ZOMBIE      = 6,
    TASK_STATE_DEAD        = 7
} task_state_t;

/* Task Flags */
#define TASK_FLAG_KTHREAD   (1u << 0)
#define TASK_FLAG_USER      (1u << 1)
#define TASK_FLAG_EXITING   (1u << 2)
#define TASK_FLAG_REALTIME  (1u << 3)
#define TASK_FLAG_IDLE      (1u << 4)

#define KERNEL_STACK_SIZE   16384u  /* 16 KiB dedicated per-task kernel stack */
#define MAX_TASK_FD         64u
#define MAX_COMM_LEN        32u

/* Basic file descriptor structure for monolithic VFS integration */
typedef struct file_descriptor {
    u32   flags;
    u64   offset;
    void* vfs_node;
} file_desc_t;

typedef struct files_struct {
    u32         count;
    file_desc_t fds[MAX_TASK_FD];
} files_struct_t;

/* Monolithic Task Structure (Process & Thread Descriptor) */
typedef struct task_struct {
    /* Identification */
    pid_t           pid;
    pid_t           ppid;
    pid_t           pgid;
    pid_t           sid;
    char            comm[MAX_COMM_LEN];

    /* Execution State & Context */
    volatile u32    state;
    u32             flags;
    registers_t     context;          /* Full CPU register frame */
    void*           kernel_stack;     /* Base pointer of 16KB kernel stack */
    void*           kernel_stack_top; /* Top pointer (RSP0) of 16KB stack */
    void*           user_stack;       /* Ring-3 user stack pointer */
    u8              fxsave_area[2688] __attribute__((aligned(64)));

    /* Memory Management */
    u32             address_space;    /* CR3 page table space / ID */
    void*           mm;               /* Pointer to mm_struct */

    /* VFS & File Descriptors */
    files_struct_t  files;

    /* Signal Handling */
    u64             sig_pending;
    u64             sig_blocked;
    void*           sig_handlers[64];

    /* Scheduling & Accounting */
    u8              priority;         /* 0 (highest) to 39 (lowest), default 20 */
    u8              policy;           /* CFS, RT, FASTLANE, IDLE */
    u32             time_slice;
    u64             vruntime;
    u64             start_time;
    u64             total_runtime;
    u16             starvation;
    u8              home_core;
    u32             migrations;
    int             has_context;

    /* Process Tree & Linked List */
    struct task_struct* parent;
    struct task_struct* first_child;
    struct task_struct* next_sibling;
    struct task_struct* next;
    struct task_struct* prev;

    /* Exit Status & Sandboxing */
    int             exit_code;
    u32             wait_child;
    int             sandbox_token;

    /* Priority inheritance */
    u8              prio_original;
    u8              prio_inherited;
    u32             blocked_by;
} __attribute__((aligned(64))) task_struct_t;

/* Legacy compatibility typedef */
typedef task_struct_t task_t;

/* Task Subsystem Core APIs */
void task_init(void);
task_struct_t* task_get_current(void);
void task_set_current(task_struct_t* task);
task_struct_t* task_find_by_pid(pid_t pid);
task_struct_t* task_alloc(void);
void task_free(task_struct_t* task);
int task_create_kthread(int (*fn)(void*), void* arg, const char* name, task_struct_t** out_task);
void task_exit(int exit_code);
void task_yield(void);

#endif /* TASK_H */
