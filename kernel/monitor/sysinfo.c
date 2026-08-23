#include "sysinfo.h"
#include "metrics.h"

void sysinfo_get(kernel_metrics_t* out) {
    metrics_snapshot(out);
}
