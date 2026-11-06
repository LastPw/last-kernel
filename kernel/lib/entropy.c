#include "entropy.h"
#include "io.h"
#include "timer.h"
#include "rtc.h"

static u32 mix(u32 value) {
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    return value ^ (value >> 16);
}

/* ─── Hardware random number generator ──────────────────────────────────────
 * RDSEED / RDRAND are detected here rather than through hardware/cpu_detect.c
 * on purpose: entropy_fill() is called from stack_protector_init() during very
 * early boot, before the CPU detection pass has run, so it cannot depend on
 * that module's cached state.
 *
 * The hardware output is *mixed into* the timer/RTC/TSC state, never used in
 * place of it.  Neither source is trusted alone: the timer-derived state is
 * guessable by anyone who can estimate boot timing, while a hardware RNG is
 * opaque and cannot be audited from here.  XOR-ing them means an attacker must
 * break both, and a broken hardware source cannot make the result worse than
 * the software state alone. */

#define RDRAND_RETRIES 10u   /* Intel's recommended bound for RDRAND */
#define RDSEED_RETRIES 32u   /* RDSEED depletes faster and needs more attempts */

#define HW_NONE   0u
#define HW_RDRAND 1u
#define HW_RDSEED 2u

/* Does the CPU implement CPUID at all?  Detected by toggling EFLAGS.ID (bit
 * 21), which is only writable on CPUs that support the instruction.  Executing
 * CPUID unconditionally on a pre-Pentium part would fault.  In long mode the
 * ID bit is always set, so this probe reports supported without faulting. */
static int cpuid_available(void) {
    unsigned long before, after;
    __asm__ __volatile__(
        "pushfq\n\t"
        "pushfq\n\t"
        "popq %0\n\t"
        "movq %0, %1\n\t"
        "xorq $0x200000, %0\n\t"
        "pushq %0\n\t"
        "popfq\n\t"
        "pushfq\n\t"
        "popq %0\n\t"
        "popfq"
        : "=&r"(after), "=&r"(before)
        :
        : "cc");
    return ((after ^ before) & 0x200000ul) != 0ul;
}

static void do_cpuid(u32 leaf, u32 subleaf, u32* a, u32* b, u32* c, u32* d) {
    __asm__ __volatile__("cpuid"
                         : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                         : "a"(leaf), "c"(subleaf));
}

/* Highest hardware source this CPU offers; computed once on first use. */
static u8 g_hw_source = 0xFFu;   /* 0xFF = not yet probed */

static u8 hw_source(void) {
    u32 a, b, c, d;

    if (g_hw_source != 0xFFu) return g_hw_source;

    g_hw_source = HW_NONE;
    if (!cpuid_available()) return g_hw_source;

    /* Leaf 0 reports the highest basic leaf the CPU understands. Querying a
     * leaf above that returns undefined data rather than zeroes, so leaf 7
     * must not be read unless it is actually supported. */
    do_cpuid(0u, 0u, &a, &b, &c, &d);
    u32 max_leaf = a;

    if (max_leaf >= 1u) {
        do_cpuid(1u, 0u, &a, &b, &c, &d);
        if (c & (1u << 30u)) g_hw_source = HW_RDRAND;   /* CPUID.1:ECX[30] */
    }
    if (max_leaf >= 7u) {
        do_cpuid(7u, 0u, &a, &b, &c, &d);
        if (b & (1u << 18u)) g_hw_source = HW_RDSEED;   /* CPUID.7.0:EBX[18] */
    }
    return g_hw_source;
}

/* Both instructions report success in CF and may legitimately fail when the
 * on-chip entropy pool is momentarily drained, so each is retried a bounded
 * number of times.  Returns 1 and sets *out on success, 0 on exhaustion. */
static int hw_random32(u32* out) {
    u32 value = 0u;
    u8 ok = 0u;
    u8 src = hw_source();

    if (src == HW_RDSEED) {
        for (u32 i = 0u; i < RDSEED_RETRIES; i++) {
            __asm__ __volatile__("rdseed %0\n\t setc %1"
                                 : "=r"(value), "=qm"(ok) :: "cc");
            if (ok) { *out = value; return 1; }
        }
        /* RDSEED exhausted: fall through to RDRAND, which is seeded from the
         * same source but reconditioned and far less likely to stall. */
        for (u32 i = 0u; i < RDRAND_RETRIES; i++) {
            __asm__ __volatile__("rdrand %0\n\t setc %1"
                                 : "=r"(value), "=qm"(ok) :: "cc");
            if (ok) { *out = value; return 1; }
        }
        return 0;
    }

    if (src == HW_RDRAND) {
        for (u32 i = 0u; i < RDRAND_RETRIES; i++) {
            __asm__ __volatile__("rdrand %0\n\t setc %1"
                                 : "=r"(value), "=qm"(ok) :: "cc");
            if (ok) { *out = value; return 1; }
        }
    }
    return 0;
}

void entropy_fill(u8* out, u32 length) {
    u32 state;
    u32 i;
    rtc_time_t now;
    if (!out) return;
    state = timer_ticks() ^ ((u32)inb(0x40) << 24) ^ ((u32)inb(0x40) << 8);
    if (rtc_get_time(&now) == 0) {
        state ^= ((u32)now.year << 16) ^ ((u32)now.month << 8) ^ now.day;
        state ^= rtc_seconds_since_midnight(&now) * 0x9E3779B9u;
    }
    {
        u32 hw;
        if (hw_random32(&hw)) state ^= hw;
    }
    for (i = 0; i < length; ++i) {
        u32 tsc_lo;
        u32 tsc_hi;
        __asm__ __volatile__("rdtsc" : "=a"(tsc_lo), "=d"(tsc_hi));
        state = mix(state ^ tsc_lo ^ (tsc_hi * 0xA511E9B3u) ^ i);
        /* Refresh from hardware once per output word.  Doing it per word rather
         * than once per call keeps the hardware contribution proportional to
         * the number of bytes produced, so a long request cannot end up mostly
         * determined by a single 32-bit draw. */
        if ((i & 3u) == 0u) {
            u32 hw;
            if (hw_random32(&hw)) state = mix(state ^ hw);
        }
        out[i] = (u8)(state >> ((i & 3u) * 8u));
    }
}
