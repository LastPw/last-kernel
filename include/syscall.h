#ifndef SYSCALL_H
#define SYSCALL_H

#include "idt.h"
#include "types.h"

#define KSYSCALL_NOP             0u
#define KSYSCALL_WRITE_CHAR      1u
#define KSYSCALL_WRITE_STR       2u
#define KSYSCALL_GET_TICKS       3u
#define KSYSCALL_ALLOC           4u
#define KSYSCALL_FREE            5u
#define KSYSCALL_COMPAT_CALL     6u
#define KSYSCALL_GET_TASK_ID     7u
#define KSYSCALL_YIELD           8u
#define KSYSCALL_TRACE_EVENT     9u
#define KSYSCALL_VERSION        10u
#define KSYSCALL_OPEN           11u
#define KSYSCALL_CLOSE          12u
#define KSYSCALL_READ           13u
#define KSYSCALL_WRITE          14u
#define KSYSCALL_SEEK           15u
#define KSYSCALL_DUP            16u
#define KSYSCALL_STAT           17u
#define KSYSCALL_EXIT           18u
#define KSYSCALL_WAIT           19u
#define KSYSCALL_EXEC           20u
#define KSYSCALL_PIPE_CREATE    21u
#define KSYSCALL_PIPE_READ      22u
#define KSYSCALL_PIPE_WRITE     23u
#define KSYSCALL_PIPE_CLOSE     24u
#define KSYSCALL_LISTEN         25u
#define KSYSCALL_ACCEPT         26u
#define KSYSCALL_SEND           27u
#define KSYSCALL_RECV           28u
#define KSYSCALL_SOCKET_CLOSE   29u
#define KSYSCALL_SPAWN          30u
#define KSYSCALL_SIGNAL         31u
#define KSYSCALL_FORK           32u
#define KSYSCALL_SOCKET_OPEN    33u
#define KSYSCALL_GFX_INFO       34u
#define KSYSCALL_GFX_MODE       35u
#define KSYSCALL_GFX_CLEAR      36u
#define KSYSCALL_GFX_RECT       37u
#define KSYSCALL_GFX_TEXT       38u
#define KSYSCALL_GFX_EXIT       39u
#define KSYSCALL_GFX_BLIT       40u
#define KSYSCALL_INPUT_READ     41u
#define KSYSCALL_GFX_ROUND_RECT 42u
#define KSYSCALL_MOUSE_READ     43u
#define KSYSCALL_SHELL_EXEC     44u
#define KSYSCALL_THREAD_CREATE  45u
#define KSYSCALL_SIGACTION      46u
#define KSYSCALL_SIGRETURN      47u
#define KSYSCALL_GETPRIO        48u
#define KSYSCALL_SETPRIO        49u
#define KSYSCALL_MMAP           50u
#define KSYSCALL_MUNMAP         51u
#define KSYSCALL_MPROTECT       52u
#define KSYSCALL_CLOCK_GETTIME  53u
#define KSYSCALL_NANOSLEEP      54u
#define SYSCALL_WAIT_BLOCKED 0xFFFFFFFEu

#define SYSCALL_POLICY_MAX_TASKS 4096u
#define SYSCALL_POLICY_MAX_NR    64u

void syscall_init(void);
void syscall_on_interrupt(registers_t* regs);
void syscall_release_task(u32 task_id);
u32 syscall_dispatch(u32 nr, u64 a0, u64 a1, u64 a2, u64 a3, u32 caller_task);
u32 syscall_version(void);
void syscall_policy_reset(u32 task_id);
int syscall_policy_set(u32 task_id, u32 nr, u8 allowed);
int syscall_policy_allowed(u32 task_id, u32 nr);
u32 syscall_policy_denied_count(u32 task_id);

#endif
