#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

void keyboard_init(void);
void keyboard_on_irq(void* regs);
int keyboard_read_char(void);
int keyboard_try_read_char(void);
void keyboard_inject_char(char c);

#endif
