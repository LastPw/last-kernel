#ifndef BOOT_LOG_H
#define BOOT_LOG_H

#include "types.h"

/* ── Boot log ring-buffer capacity ─────────────────────────────────────── */
#define BOOT_LOG_CAP 128u

/* ── Boot log event types ──────────────────────────────────────────────── */
#define BOOT_LOG_STAGE_START   1u  /* A kinit stage began */
#define BOOT_LOG_STAGE_OK      2u  /* A kinit stage completed successfully */
#define BOOT_LOG_STAGE_FAIL    3u  /* A kinit stage had failures */
#define BOOT_LOG_MODULE_OK     4u  /* An individual init module succeeded */
#define BOOT_LOG_MODULE_FAIL   5u  /* An individual init module failed */
#define BOOT_LOG_POLICY        6u  /* Boot policy or mode decision */
#define BOOT_LOG_FLAG          7u  /* Boot flag summary */
#define BOOT_LOG_INFO          8u  /* Informational message */
#define BOOT_LOG_DONE          9u  /* Boot sequence complete */
#define BOOT_LOG_ERROR        10u  /* Boot error (non-fatal) */
#define BOOT_LOG_KSHELL       11u  /* Shell startup */

/* ── Boot log entry ────────────────────────────────────────────────────── */
typedef struct {
    u32 tick;                       /* timer_ticks() when the event occurred */
    u32 type;                       /* BOOT_LOG_* event type */
    i32 result;                     /* Result code (0 = OK, < 0 = error) */
    char desc[40];                  /* Human-readable description */
    u32 detail;                     /* Extra numeric detail (e.g. module count) */
} boot_log_entry_t;

/* ── Public API ────────────────────────────────────────────────────────── */

/* Initialise the boot log ring buffer.  Safe to call more than once. */
void boot_log_init(void);

/* Push a single entry.
 * type   — one of the BOOT_LOG_* constants.
 * result — 0 for OK, negative for errors.
 * desc   — short description (copied, may be NULL).
 * detail — optional extra numeric value. */
void boot_log_push(u32 type, i32 result, const char* desc, u32 detail);

/* Convenience wrapper: push a STAGE_OK / STAGE_FAIL entry for a kinit
 * stage identified by its level name. */
void boot_log_stage(u32 level_idx, const char* level_name, int ok);

/* Convenience wrapper: push a MODULE_OK / MODULE_FAIL entry. */
void boot_log_module(const char* module_name, int rc);

/* Return the total number of entries pushed so far (may exceed CAP). */
u32 boot_log_count(void);

/* Retrieve the n-th most recent entry (0 = latest).
 * Returns 0 on success, -1 if index is out of range. */
int boot_log_get(u32 reverse_index, boot_log_entry_t* out);

/* Clear the ring buffer. */
void boot_log_clear(void);

#endif /* BOOT_LOG_H */
