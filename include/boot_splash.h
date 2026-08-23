#ifndef BOOT_SPLASH_H
#define BOOT_SPLASH_H

#include "types.h"

/* ── Kinit stage names shown on the splash ─────────────────────────────── */
#define SPLASH_NUM_STAGES 8u

extern const char* g_splash_stage_names[SPLASH_NUM_STAGES];

/* ── API ───────────────────────────────────────────────────────────────── */

/* Initialize the framebuffer and draw the boot splash.
 * Must be called after paging is online (after kinit MM stage).
 * Returns 0 on success, -1 if no framebuffer is available. */
int boot_splash_init(void);

/* Update the progress bar to reflect the current stage index (0-based).
 * stage_idx should be 0..SPLASH_NUM_STAGES-1.
 * label is an optional sub-stage label (may be NULL). */
void boot_splash_update(u32 stage_idx, const char* label);

/* Advance the progress by one stage and display the stage name.
 * Convenience wrapper around boot_splash_update. */
void boot_splash_advance(void);

/* Signal that boot is complete — optionally draw a "ready" state.
 * The splash surface remains in the framebuffer until LastGUI or the
 * shell takes over. */
void boot_splash_done(void);

/* Check if the splash is currently active. */
int boot_splash_active(void);

#endif /* BOOT_SPLASH_H */
