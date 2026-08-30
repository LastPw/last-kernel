#include "libsys.h"
#include <stdarg.h>

void exit(int status) {
    syscall1(SYS_EXIT, (u64)status);
    while (1) { __asm__ __volatile__("pause"); }
}

i32 write(int fd, const void* buf, size_t count) {
    return (i32)syscall3(SYS_WRITE, (u64)fd, (u64)(uintptr_t)buf, (u64)count);
}

i32 read(int fd, void* buf, size_t count) {
    return (i32)syscall3(SYS_READ, (u64)fd, (u64)(uintptr_t)buf, (u64)count);
}

int open(const char* path, int flags) {
    return (int)syscall2(SYS_OPEN, (u64)(uintptr_t)path, (u64)flags);
}

int close(int fd) {
    return (int)syscall1(SYS_CLOSE, (u64)fd);
}

i32 getpid(void) {
    return (i32)syscall0(SYS_GET_TASK_ID);
}

void yield(void) {
    syscall0(SYS_YIELD);
}

u32 getticks(void) {
    return (u32)syscall0(SYS_GET_TICKS);
}

int socket_open(u32 remote_ip, u16 remote_port, u32 local_ip, u16 local_port) {
    return (int)syscall4(SYS_SOCKET_OPEN, remote_ip, remote_port, local_ip, local_port);
}

i32 socket_send(int socket_id, const void* buf, size_t len) {
    return (i32)syscall3(SYS_SEND, (u64)socket_id, (u64)(uintptr_t)buf, (u64)len);
}

i32 socket_recv(int socket_id, void* buf, size_t len) {
    return (i32)syscall3(SYS_RECV, (u64)socket_id, (u64)(uintptr_t)buf, (u64)len);
}

int socket_close(int socket_id) {
    return (int)syscall1(SYS_SOCKET_CLOSE, (u64)socket_id);
}

size_t strlen(const char* s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    if (n == 0) return 0;
    while (n-- && *s1 && *s2 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* strcpy(char* dest, const char* src) {
    if (!dest || !src) return dest;
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    if (!dest || !src) return dest;
    char* d = dest;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dest;
}

void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dest;
}

void print(const char* str) {
    if (!str) return;
    write(1, str, strlen(str));
}

void println(const char* str) {
    print(str);
    print("\n");
}

void print_dec(i64 n) {
    char buf[32];
    if (n == 0) { print("0"); return; }
    if (n < 0) { print("-"); n = -n; }
    int i = 0;
    while (n > 0) {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    for (int j = i - 1; j >= 0; j--) {
        char c[2] = { buf[j], 0 };
        print(c);
    }
}

void print_hex(u64 n) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = hex[(n >> (60 - 4 * i)) & 0xF];
    }
    buf[18] = '\0';
    print(buf);
}
