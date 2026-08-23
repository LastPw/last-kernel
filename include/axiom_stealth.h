#ifndef LASTOS_AXIOM_STEALTH_H
#define LASTOS_AXIOM_STEALTH_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AxiomStealth Anti-Fingerprint & Cloudflare Challenge Solver
 * Eliminates bot detection, CAPTCHA blocks, and Cloudflare Turnstile barriers
 */

typedef struct {
    const char* user_agent;
    const char* sec_ch_ua;
    const char* sec_ch_ua_platform;
    const char* accept_language;
    u8          webdriver_masked;
    u8          chrome_runtime_injected;
    u8          cf_clearance_active;
    char        cf_clearance_token[64];
} stealth_context_t;

void  stealth_init(void);
const stealth_context_t* stealth_get_context(void);
u8    stealth_is_cloudflare_challenge(const char* html, u32 len);
int   stealth_solve_cloudflare_challenge(const char* challenge_html, char* out_solved_html, u32 max_len);

#ifdef __cplusplus
}
#endif

#endif /* LASTOS_AXIOM_STEALTH_H */
