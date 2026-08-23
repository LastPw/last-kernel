#ifndef IO_H
#define IO_H

#include "types.h"

static inline u8 inb(u16 port) {
    u8 value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline u16 inw(u16 port) {
    u16 value;
    __asm__ __volatile__("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(u16 port, u8 value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(u16 port, u16 value) {
    __asm__ __volatile__("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline u32 inl(u16 port) {
    u32 value;
    __asm__ __volatile__("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(u16 port, u32 value) {
    __asm__ __volatile__("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline u8 mmio_read8(u64 address) {
    return *(volatile u8*)address;
}

static inline u16 mmio_read16(u64 address) {
    return *(volatile u16*)address;
}

static inline u32 mmio_read32(u64 address) {
    return *(volatile u32*)address;
}

static inline void mmio_write8(u64 address, u8 value) {
    *(volatile u8*)address = value;
}

static inline void mmio_write16(u64 address, u16 value) {
    *(volatile u16*)address = value;
}

static inline void mmio_write32(u64 address, u32 value) {
    *(volatile u32*)address = value;
}

static inline u64 rdmsr(u32 msr) {
    u32 low, high;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((u64)high << 32) | low;
}

static inline void wrmsr(u32 msr, u64 val) {
    u32 low = (u32)val;
    u32 high = (u32)(val >> 32);
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

#endif
