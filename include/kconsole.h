#ifndef KCONSOLE_H
#define KCONSOLE_H

#include "types.h"

/* ── Pure In-Kernel Debug & Diagnostics Console (KConsole) ── */

void kconsole_init(void);
void kconsole_run(void);
void kconsole_resume(void);
int  kconsole_exec_command(const char* cmd_line, char* out_buf, u32 out_buf_len);

#endif /* KCONSOLE_H */
