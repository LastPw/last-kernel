#ifndef LIBSYS_H
#define LIBSYS_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed char        i8;
typedef signed short       i16;
typedef signed int         i32;
typedef signed long long   i64;
typedef unsigned long long size_t;
typedef unsigned long long uintptr_t;

#define NULL ((void*)0)

#define SYS_WRITE_CHAR      1
#define SYS_WRITE_STR       2
#define SYS_GET_TICKS       3
#define SYS_ALLOC           4
#define SYS_FREE            5
#define SYS_GET_TASK_ID     7
#define SYS_YIELD           8
#define SYS_VERSION         10
#define SYS_OPEN            11
#define SYS_CLOSE           12
#define SYS_READ            13
#define SYS_WRITE           14
#define SYS_SEEK            15
#define SYS_EXIT            18
#define SYS_WAIT            19
#define SYS_EXEC            20
#define SYS_SPAWN           30
#define SYS_FORK            32

static inline u64 syscall0(u64 nr) {
    u64 ret;
    __asm__ __volatile__ ("int $0x80" : "=a"(ret) : "a"(nr) : "memory");
    return ret;
}

static inline u64 syscall1(u64 nr, u64 a0) {
    u64 ret;
    __asm__ __volatile__ ("int $0x80" : "=a"(ret) : "a"(nr), "b"(a0) : "memory");
    return ret;
}

static inline u64 syscall2(u64 nr, u64 a0, u64 a1) {
    u64 ret;
    __asm__ __volatile__ ("int $0x80" : "=a"(ret) : "a"(nr), "b"(a0), "c"(a1) : "memory");
    return ret;
}

static inline u64 syscall3(u64 nr, u64 a0, u64 a1, u64 a2) {
    u64 ret;
    __asm__ __volatile__ ("int $0x80" : "=a"(ret) : "a"(nr), "b"(a0), "c"(a1), "d"(a2) : "memory");
    return ret;
}

static inline u64 syscall4(u64 nr, u64 a0, u64 a1, u64 a2, u64 a3) {
    u64 ret;
    __asm__ __volatile__ ("int $0x80" : "=a"(ret) : "a"(nr), "b"(a0), "c"(a1), "d"(a2), "S"(a3) : "memory");
    return ret;
}

#define SYS_LISTEN          25
#define SYS_ACCEPT          26
#define SYS_SEND            27
#define SYS_RECV            28
#define SYS_SOCKET_CLOSE    29
#define SYS_SOCKET_OPEN     33

void  exit(int status);
i32   write(int fd, const void* buf, size_t count);
i32   read(int fd, void* buf, size_t count);
int   open(const char* path, int flags);
int   close(int fd);
i32   getpid(void);
void  yield(void);
u32   getticks(void);

/* Networking Sockets */
int   socket_open(u32 remote_ip, u16 remote_port, u32 local_ip, u16 local_port);
i32   socket_send(int socket_id, const void* buf, size_t len);
i32   socket_recv(int socket_id, void* buf, size_t len);
int   socket_close(int socket_id);

size_t strlen(const char* s);
int    strcmp(const char* s1, const char* s2);
int    strncmp(const char* s1, const char* s2, size_t n);
char*  strcpy(char* dest, const char* src);
char*  strncpy(char* dest, const char* src, size_t n);
void*  memset(void* s, int c, size_t n);
void*  memcpy(void* dest, const void* src, size_t n);

void   print(const char* str);
void   println(const char* str);
void   print_dec(i64 n);
void   print_hex(u64 n);
int    printf(const char* fmt, ...);

#endif /* LIBSYS_H */
