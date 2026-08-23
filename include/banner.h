#ifndef BANNER_H
#define BANNER_H

#include "types.h"

const char* banner_guess_service(u16 port);
void banner_on_open_port(u32 target_ip, u16 port);

#endif
