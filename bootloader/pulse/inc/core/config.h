/* pulse.conf v2 — global keys + named entries.
 *
 *   timeout=5                # seconds until default boots (0 = immediate)
 *   default=0                # index of default entry
 *
 *   [entry name]
 *   kernel=/boot/k32.elf     # required per entry
 *   cmdline=whatever
 *
 * Files without any section header get one implicit entry (v1 compat).
 */
#ifndef PULSE_CONFIG_H
#define PULSE_CONFIG_H

#include "pulsebase.h"

#define PULSE_MAX_ENTRIES 8
#define CFG_TITLE_MAX     64
#define CFG_STR_MAX       256

typedef struct {
    char title[CFG_TITLE_MAX];
    char kernel[CFG_STR_MAX];      /* required */
    char cmdline[CFG_STR_MAX];
} cfg_entry_t;

typedef struct {
    UINT32 timeout;                /* seconds, 0 = boot immediately */
    UINT32 default_idx;
    UINTN  count;
    cfg_entry_t entry[PULSE_MAX_ENTRIES];
} pulse_config_t;

/* Parse len bytes. Returns 0 on success, -1 syntax error (line in
 * *err_line), -2 if no usable entry exists. cfg is zeroed first. */
INT32 config_parse(const char *data, UINTN len,
                   pulse_config_t *cfg, UINTN *err_line);

#endif
