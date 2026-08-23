#ifndef EBPF_H
#define EBPF_H

#include "types.h"

typedef struct {
    u8 code;
    u8 dst_reg:4;
    u8 src_reg:4;
    s16 off;
    i32 imm;
} ebpf_insn_t;

typedef struct {
    u64 regs[11]; // R0..R10
    u32 insn_count;
    ebpf_insn_t insns[64];
} ebpf_vm_t;

int ebpf_init(void);
int ebpf_load_program(const ebpf_insn_t* insns, u32 count, ebpf_vm_t* out_vm);
u64 ebpf_exec(ebpf_vm_t* vm, void* ctx);

#endif
