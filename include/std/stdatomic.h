#ifndef LASTOS_FS_STDATOMIC_H
#define LASTOS_FS_STDATOMIC_H

#include "stdint.h"
#include "stddef.h"

typedef enum {
    memory_order_relaxed = __ATOMIC_RELAXED,
    memory_order_consume = __ATOMIC_CONSUME,
    memory_order_acquire = __ATOMIC_ACQUIRE,
    memory_order_release = __ATOMIC_RELEASE,
    memory_order_acq_rel = __ATOMIC_ACQ_REL,
    memory_order_seq_cst = __ATOMIC_SEQ_CST
} memory_order;

#define _Atomic(T) struct { volatile T __val; }
typedef _Atomic(int)           atomic_int;
typedef _Atomic(unsigned int)  atomic_uint;
typedef _Atomic(unsigned long) atomic_ulong;

#define atomic_init(obj, val)           ((obj)->__val = (val))
#define atomic_load(obj)                __atomic_load_n(&(obj)->__val, __ATOMIC_SEQ_CST)
#define atomic_load_explicit(obj, mo)   __atomic_load_n(&(obj)->__val, mo)
#define atomic_store(obj, val)          __atomic_store_n(&(obj)->__val, val, __ATOMIC_SEQ_CST)
#define atomic_store_explicit(obj, val, mo) __atomic_store_n(&(obj)->__val, val, mo)
#define atomic_fetch_add(obj, val)      __atomic_fetch_add(&(obj)->__val, val, __ATOMIC_SEQ_CST)
#define atomic_fetch_add_explicit(obj, val, mo) __atomic_fetch_add(&(obj)->__val, val, mo)
#define atomic_fetch_sub(obj, val)      __atomic_fetch_sub(&(obj)->__val, val, __ATOMIC_SEQ_CST)
#define atomic_fetch_sub_explicit(obj, val, mo) __atomic_fetch_sub(&(obj)->__val, val, mo)
#define atomic_fetch_and(obj, val)      __atomic_fetch_and(&(obj)->__val, val, __ATOMIC_SEQ_CST)
#define atomic_fetch_or(obj, val)       __atomic_fetch_or(&(obj)->__val, val, __ATOMIC_SEQ_CST)
#define atomic_fetch_xor(obj, val)      __atomic_fetch_xor(&(obj)->__val, val, __ATOMIC_SEQ_CST)
#define atomic_exchange(obj, val)       __atomic_exchange_n(&(obj)->__val, val, __ATOMIC_SEQ_CST)
#define atomic_compare_exchange_strong(obj, expected, desired) \
    __atomic_compare_exchange_n(&(obj)->__val, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

#endif /* LASTOS_FS_STDATOMIC_H */
