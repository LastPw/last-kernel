#include "bearssl.h"
#include "entropy.h"

/*
 * BearSSL asks for an operating-system seeder even when the caller injects
 * entropy explicitly. LastOS has no libc-backed /dev/urandom yet, so we
 * provide a custom seeder backed by the kernel entropy pool.
 *
 * SECURITY: returning NULL here would leave TLS handshakes dependent
 * solely on manually injected entropy.  If the injection path is ever
 * skipped the PRNG would be predictable.  This seeder guarantees the
 * PRNG always receives at least 256 bits of entropy.
 */
static int lastos_seeder(const br_prng_class **ctx)
{
    u8 seed[32];
    entropy_fill(seed, sizeof(seed));
    (*ctx)->update(ctx, seed, sizeof(seed));
    return 1;
}

br_prng_seeder
br_prng_seeder_system(const char **name)
{
    if (name) *name = "lastos-entropy";
    return lastos_seeder;
}
