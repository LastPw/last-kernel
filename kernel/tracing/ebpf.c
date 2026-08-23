#include "ebpf.h"
#include "serial.h"
#include "string.h"

static u8 g_ebpf_initialized = 0;

int ebpf_init(void) {
    g_ebpf_initialized = 1;
    serial_writeln("[EBPF] Extended Berkeley Packet Filter (eBPF) 64-bit Virtual Machine Initialized");
    return 0;
}

int ebpf_load_program(const ebpf_insn_t* insns, u32 count, ebpf_vm_t* out_vm) {
    if (!g_ebpf_initialized || !insns || !out_vm || count == 0 || count > 64) return -1;
    memset(out_vm, 0, sizeof(ebpf_vm_t));
    memcpy(out_vm->insns, insns, count * sizeof(ebpf_insn_t));
    out_vm->insn_count = count;
    return 0;
}

u64 ebpf_exec(ebpf_vm_t* vm, void* ctx) {
    if (!vm) return 0;
    vm->regs[1] = (u64)(uintptr_t)ctx; // R1 = context pointer
    for (u32 pc = 0; pc < vm->insn_count; pc++) {
        ebpf_insn_t insn = vm->insns[pc];
        u8 opcode = insn.code;
        if (opcode == 0x95) { // BPF_JMP | BPF_EXIT
            break;
        } else if (opcode == 0x07) { // BPF_ALU64 | BPF_ADD
            vm->regs[insn.dst_reg] += (u64)insn.imm;
        } else if (opcode == 0xB7) { // BPF_ALU64 | BPF_MOV
            vm->regs[insn.dst_reg] = (u64)insn.imm;
        }
    }
    return vm->regs[0]; // R0 = return value
}
