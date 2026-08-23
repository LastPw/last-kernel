#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "types.h"

typedef struct {
    u64 entry;
    u64 image_base;
    u64 image_limit;
    u64 stack_bottom;
    u64 stack_top;
    u64 stack_guard;
    u32 load_segments;
    u32 mapped_pages;
    u32 address_space;
} elf_exec_info_t;

int elf32_load_user(const u8* image, u32 image_len, u32 task_id, elf_exec_info_t* out);
int elf32_destroy_user(u32 task_id);
int elf32_clone_user(u32 parent_task, u32 child_task, elf_exec_info_t* out);
int elf32_handle_page_fault(u32 task_id, u64 fault_addr, u32 err_code);
int elf32_guard_addr(u32 task_id, u64* out);
u64 elf32_user_alloc(u32 task_id, u32 size);
int elf32_user_free(u32 task_id, u64 ptr);

#endif