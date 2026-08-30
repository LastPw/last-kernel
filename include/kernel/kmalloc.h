#ifndef KMALLOC_H
#define KMALLOC_H

#include "types.h"
#include "stddef.h"

/* General purpose monolithic kernel dynamic memory allocation */
void* kmalloc(size_t size);
void* kzalloc(size_t size);
void  kfree(void* ptr);

#endif /* KMALLOC_H */
