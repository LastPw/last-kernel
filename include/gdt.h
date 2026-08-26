#ifndef GDT_H
#define GDT_H

#include "types.h"

void gdt_init(void);
void gdt_set_kernel_stack(u64 rsp0);

void* gdt_descriptor(void);
#endif
