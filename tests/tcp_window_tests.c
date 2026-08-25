/* Host-side TCP window/flow-control unit tests (pure logic from
 * net/tcp_window.c).  Run by `make test`. */

#include <stdio.h>
#include "tcp_window.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("  PASS %s\n", (name)); \
    else { printf("  FAIL %s\n", (name)); failures++; } \
} while (0)

int main(void) {
    printf("TCP window/flow-control tests:\n");

    /* effective window = min(cwnd, peer) */
    CHECK(tcp_eff_window(4096, 8192) == 4096, "eff: cwnd-limited");
    CHECK(tcp_eff_window(16384, 2048) == 2048, "eff: peer-limited");
    CHECK(tcp_eff_window(0, 65535) == 0, "eff: zero cwnd stalls");

    /* send gating */
    CHECK(tcp_can_send(0, 1024, 4096) == 1, "gate: idle + fits");
    CHECK(tcp_can_send(3072, 1024, 4096) == 1, "gate: exactly fills");
    CHECK(tcp_can_send(3073, 1024, 4096) == 0, "gate: exceeds by 1");
    CHECK(tcp_can_send(0, 0, 65535) == 0, "gate: zero-len never sends");
    CHECK(tcp_can_send(0, 1024, 0) == 0, "gate: zero window blocks");
    CHECK(tcp_can_send(5000, 100, 4096) == 0, "gate: unacked > window");

    /* advertised window scaling */
    CHECK(tcp_adv_from_slots(0, 16, 65535) == 65535, "adv: empty = full");
    CHECK(tcp_adv_from_slots(16, 16, 65535) == 0, "adv: full queue = 0");
    u32 half = tcp_adv_from_slots(8, 16, 65535);
    CHECK(half >= 512 && half <= 65535, "adv: half in range");
    u32 quarter = tcp_adv_from_slots(12, 16, 65535);
    CHECK(quarter < half, "adv: more queued -> smaller window");
    CHECK(tcp_adv_from_slots(15, 16, 65535) >= 512,
          "adv: floor prevents total stall near full");

    /* RTO backoff clamping */
    CHECK(tcp_backoff_rto(10) == TCP_RTO_MIN_TICKS, "backoff: below min clamps");
    CHECK(tcp_backoff_rto(500) == 500, "backoff: mid-range preserved");
    CHECK(tcp_backoff_rto(999999) == TCP_RTO_MAX_TICKS, "backoff: above max clamps");
    {
        u32 rto = 100;
        for (int i = 0; i < 20; i++) rto = tcp_backoff_rto(rto * 2);
        CHECK(rto == TCP_RTO_MAX_TICKS, "backoff: exponential converges to max");
    }

    if (failures == 0) {
        printf("TCP window tests: all vectors pass\n");
        return 0;
    }
    printf("TCP window tests: %d FAILURES\n", failures);
    return 1;
}
