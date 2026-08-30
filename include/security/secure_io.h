#ifndef SECURE_IO_H
#define SECURE_IO_H

#include "types.h"

void secure_io_init(void);
int secure_io_keyboard_read_char(void);
int secure_io_keyboard_try_read_char(void);

#endif
