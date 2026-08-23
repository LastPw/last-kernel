#ifndef LASTOS_FS_MALLOC_H
#define LASTOS_FS_MALLOC_H

#include "stdlib.h"

static inline size_t malloc_usable_size(void *p) {
    (void)p;
    return 0;
}

#endif /* LASTOS_FS_MALLOC_H */
