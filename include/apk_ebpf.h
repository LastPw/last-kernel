#ifndef APK_EBPF_H
#define APK_EBPF_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EBPF_REG_COUNT 11

/* eBPF Instruction Format (64-bit) */
typedef struct {
    u8  opcode;
    u8  dst_reg:4;
    u8  src_reg:4;
    s16 offset;
    i32 imm;
} __attribute__((packed)) ebpf_insn_t;

/* eBPF Map Types */
#define EBPF_MAP_TYPE_HASH       1
#define EBPF_MAP_TYPE_ARRAY      2
#define EBPF_MAP_TYPE_RINGBUF    3

#define EBPF_MAX_MAP_ENTRIES     256
#define EBPF_MAX_MAPS            16

typedef struct {
    u32 map_id;
    u32 map_type;
    u32 key_size;
    u32 value_size;
    u32 max_entries;
    u32 count;
    u8  keys[EBPF_MAX_MAP_ENTRIES][16];
    u8  values[EBPF_MAX_MAP_ENTRIES][64];
} ebpf_map_t;

typedef struct {
    u64 regs[EBPF_REG_COUNT];
    u8  stack[512];
    u8  is_halted;
    u64 exit_code;
} ebpf_vm_t;

int  ebpf_init(void);
int  ebpf_verify_program(const ebpf_insn_t* insns, u32 count);
int  ebpf_run_program(const ebpf_insn_t* insns, u32 count, void* ctx, u64* out_result);

int  ebpf_map_create(u32 type, u32 key_size, u32 value_size, u32 max_entries);
int  ebpf_map_update(u32 map_id, const void* key, const void* value);
int  ebpf_map_lookup(u32 map_id, const void* key, void* out_value);

#ifdef __cplusplus
}
#endif

#endif /* APK_EBPF_H */
