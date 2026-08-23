#include "timing_engine.h"

void timing_engine_build(u8 template_id, u32 timer_hz, timing_profile_t* out) {
    if (!out) {
        return;
    }
    if (timer_hz == 0) {
        timer_hz = 100;
    }
    if (template_id > 5) {
        template_id = 3;
    }

    out->template_id = template_id;
    out->timeout_ticks = timer_hz;
    out->max_retries = 2;
    out->inter_probe_delay_ticks = 1;

    if (template_id == 0) {
        out->timeout_ticks = timer_hz * 5;
        out->max_retries = 4;
        out->inter_probe_delay_ticks = timer_hz / 5;
        if (out->inter_probe_delay_ticks == 0) {
            out->inter_probe_delay_ticks = 1;
        }
    } else if (template_id == 1) {
        out->timeout_ticks = timer_hz * 3;
        out->max_retries = 3;
        out->inter_probe_delay_ticks = timer_hz / 10;
        if (out->inter_probe_delay_ticks == 0) {
            out->inter_probe_delay_ticks = 1;
        }
    } else if (template_id == 2) {
        out->timeout_ticks = timer_hz * 2;
        out->max_retries = 3;
        out->inter_probe_delay_ticks = timer_hz / 20;
        if (out->inter_probe_delay_ticks == 0) {
            out->inter_probe_delay_ticks = 1;
        }
    } else if (template_id == 4) {
        out->timeout_ticks = timer_hz / 2;
        if (out->timeout_ticks == 0) {
            out->timeout_ticks = 1;
        }
        out->max_retries = 1;
        out->inter_probe_delay_ticks = 1;
    } else if (template_id == 5) {
        out->timeout_ticks = timer_hz / 3;
        if (out->timeout_ticks == 0) {
            out->timeout_ticks = 1;
        }
        out->max_retries = 1;
        out->inter_probe_delay_ticks = 1;
    }
}
