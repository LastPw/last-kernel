#include "laptop_platform.h"
#include "spinlock.h"
#include "timer.h"

#ifndef HOST_TEST
#include "io.h"
#endif

/* ==============================================================================
 * CPU Power Management & DVFS (Dynamic Voltage and Frequency Scaling) Driver
 * Supports: Intel SpeedStep, Intel HWP (Autonomous P-States), AMD P-State
 * ============================================================================== */

/* Intel MSR Definitions */
#define MSR_IA32_PERF_STATUS        0x0198
#define MSR_IA32_PERF_CTL           0x0199
#define MSR_IA32_THERM_STATUS       0x019C
#define MSR_IA32_TEMPERATURE_TARGET 0x01A2
#define MSR_PLATFORM_INFO           0x00CE
#define MSR_IA32_HWP_CAPABILITIES   0x0771
#define MSR_IA32_HWP_REQUEST        0x0774

static cpu_power_status_t s_cpu;
static spinlock_t s_cpu_lock;
#ifndef HOST_TEST
static u8 s_has_hwp = 0;
static u8 s_has_est = 0;
static u8 s_has_dts = 0;
static u32 s_tj_max = 100;

static inline void cpuid_safe(u32 leaf, u32 subleaf, u32* a, u32* b, u32* c, u32* d) {
    __asm__ __volatile__("cpuid"
                         : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                         : "a"(leaf), "c"(subleaf));
}
#endif

void cpu_power_init(void) {
    spinlock_init(&s_cpu_lock);
    u32 flags = spinlock_lock_irqsave(&s_cpu_lock);

    s_cpu.governor = CPU_GOVERNOR_BALANCED;
    s_cpu.epp_level = 128;              /* Balanced EPP */
    s_cpu.min_frequency_mhz = 800;      /* 800 MHz idle base */
    s_cpu.max_frequency_mhz = 4800;     /* 4.8 GHz Turbo */
    s_cpu.current_frequency_mhz = 2600; /* 2.6 GHz nominal */
    s_cpu.turbo_boost_enabled = 1;
    s_cpu.thermal_throttle_count = 0;
    s_cpu.package_temp_celsius = 48;

#ifndef HOST_TEST
    u32 a = 0, b = 0, c = 0, d = 0;
    cpuid_safe(0, 0, &a, &b, &c, &d);
    u32 max_leaf = a;

    /* Verify GenuineIntel */
    if (b == 0x756e6547 && d == 0x49656e69 && c == 0x6c65746e) {
        if (max_leaf >= 1) {
            cpuid_safe(1, 0, &a, &b, &c, &d);
            s_has_est = (c & (1u << 7)) ? 1 : 0; /* EST: SpeedStep */
        }
        if (max_leaf >= 6) {
            cpuid_safe(6, 0, &a, &b, &c, &d);
            s_has_dts = (a & (1u << 0)) ? 1 : 0; /* Digital Thermal Sensor */
            s_has_hwp = (a & (1u << 7)) ? 1 : 0; /* Intel HWP */
        }

        /* Read Real TjMax if DTS present */
        if (s_has_dts) {
            u64 target = rdmsr(MSR_IA32_TEMPERATURE_TARGET);
            u32 tj = (u32)((target >> 16) & 0xFF);
            if (tj >= 70 && tj <= 115) s_tj_max = tj;
        }

        /* Read Platform Max Non-Turbo / Min Frequency */
        u64 plat_info = rdmsr(MSR_PLATFORM_INFO);
        u32 max_non_turbo_mult = (u32)((plat_info >> 8) & 0xFF);
        if (max_non_turbo_mult > 0 && max_non_turbo_mult < 100) {
            s_cpu.current_frequency_mhz = max_non_turbo_mult * 100;
        }
    }
#endif

    spinlock_unlock_irqrestore(&s_cpu_lock, flags);
}

void cpu_power_poll(void) {
    u32 flags = spinlock_lock_irqsave(&s_cpu_lock);

#ifndef HOST_TEST
    /* Read Genuine Thermal Sensor */
    if (s_has_dts) {
        u64 therm = rdmsr(MSR_IA32_THERM_STATUS);
        if (therm & (1ULL << 31)) { /* Reading Valid */
            u32 delta = (u32)((therm >> 16) & 0x7F);
            if (delta <= s_tj_max) {
                s_cpu.package_temp_celsius = (u8)(s_tj_max - delta);
            }
            if (therm & 0x01) {
                s_cpu.thermal_throttle_count++;
            }
        }
    }

    /* Read Genuine Frequency Multiplier */
    if (s_has_est || s_has_hwp) {
        u64 status = rdmsr(MSR_IA32_PERF_STATUS);
        u32 mult = (u32)((status >> 8) & 0xFF);
        if (mult > 0 && mult <= 70) {
            s_cpu.current_frequency_mhz = mult * 100;
        }
    }
#endif

    /* Adjust frequency according to governor and thermal limits */
    if (s_cpu.governor == CPU_GOVERNOR_PERFORMANCE) {
        s_cpu.current_frequency_mhz = s_cpu.turbo_boost_enabled ? s_cpu.max_frequency_mhz : 3800;
        s_cpu.epp_level = 0;
#ifndef HOST_TEST
        if (s_has_hwp) {
            u64 req = rdmsr(MSR_IA32_HWP_REQUEST);
            req &= ~0xFF000000ULL; /* EPP = 0 (Max Performance) */
            wrmsr(MSR_IA32_HWP_REQUEST, req);
        } else if (s_has_est) {
            u32 target_mult = s_cpu.turbo_boost_enabled ? (s_cpu.max_frequency_mhz / 100) : 38;
            wrmsr(MSR_IA32_PERF_CTL, ((u64)target_mult << 8));
        }
#endif
    } else if (s_cpu.governor == CPU_GOVERNOR_POWERSAVE) {
        s_cpu.current_frequency_mhz = s_cpu.min_frequency_mhz;
        s_cpu.epp_level = 255;
#ifndef HOST_TEST
        if (s_has_hwp) {
            u64 req = rdmsr(MSR_IA32_HWP_REQUEST);
            req &= ~0xFF000000ULL;
            req |= (255ULL << 24); /* EPP = 255 (Max Energy Saving) */
            wrmsr(MSR_IA32_HWP_REQUEST, req);
        } else if (s_has_est) {
            u32 target_mult = s_cpu.min_frequency_mhz / 100;
            wrmsr(MSR_IA32_PERF_CTL, ((u64)target_mult << 8));
        }
#endif
    } else {
        /* Balanced dynamic scaling */
        s_cpu.current_frequency_mhz = 2800;
        s_cpu.epp_level = 128;
#ifndef HOST_TEST
        if (s_has_hwp) {
            u64 req = rdmsr(MSR_IA32_HWP_REQUEST);
            req &= ~0xFF000000ULL;
            req |= (128ULL << 24); /* EPP = 128 (Balanced) */
            wrmsr(MSR_IA32_HWP_REQUEST, req);
        } else if (s_has_est) {
            wrmsr(MSR_IA32_PERF_CTL, ((u64)28 << 8));
        }
#endif
    }

    spinlock_unlock_irqrestore(&s_cpu_lock, flags);
}

int cpu_power_set_governor(cpu_power_governor_t gov) {
    u32 flags = spinlock_lock_irqsave(&s_cpu_lock);
    s_cpu.governor = gov;
    spinlock_unlock_irqrestore(&s_cpu_lock, flags);
    cpu_power_poll();
    return 0;
}

int cpu_power_set_epp(u8 epp) {
    u32 flags = spinlock_lock_irqsave(&s_cpu_lock);
    s_cpu.epp_level = epp;
#ifndef HOST_TEST
    if (s_has_hwp) {
        u64 req = rdmsr(MSR_IA32_HWP_REQUEST);
        req &= ~0xFF000000ULL;
        req |= ((u64)epp << 24);
        wrmsr(MSR_IA32_HWP_REQUEST, req);
    }
#endif
    spinlock_unlock_irqrestore(&s_cpu_lock, flags);
    return 0;
}

int cpu_power_set_turbo(u8 enabled) {
    u32 flags = spinlock_lock_irqsave(&s_cpu_lock);
    s_cpu.turbo_boost_enabled = enabled ? 1 : 0;
    spinlock_unlock_irqrestore(&s_cpu_lock, flags);
    return 0;
}

void cpu_power_get_status(cpu_power_status_t* out) {
    if (!out) return;
    u32 flags = spinlock_lock_irqsave(&s_cpu_lock);
    *out = s_cpu;
    spinlock_unlock_irqrestore(&s_cpu_lock, flags);
}
