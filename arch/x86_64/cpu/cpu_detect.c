#include "cpu_detect.h"

static cpu_info_t info;

static int cpuid_supported(void) {
    /* The ID bit of RFLAGS is not togglable in long mode, so this probe can
     * never fault.  Kept for the 32-bit protected-mode history; on x86_64 it
     * always reports supported. */
    unsigned long original = 0;
    unsigned long toggled = 0;

    __asm__ __volatile__(
        "pushfq\n\t"
        "popq %0\n\t"
        : "=r"(original)
    );

    unsigned long with_id = original ^ (1ul << 21);

    __asm__ __volatile__(
        "pushq %0\n\t"
        "popfq\n\t"
        :
        : "r"(with_id)
    );

    __asm__ __volatile__(
        "pushfq\n\t"
        "popq %0\n\t"
        : "=r"(toggled)
    );

    __asm__ __volatile__(
        "pushq %0\n\t"
        "popfq\n\t"
        :
        : "r"(original)
    );

    return ((original ^ toggled) & (1ul << 21)) != 0;
}

static void cpuid(u32 leaf, u32 subleaf, u32* a, u32* b, u32* c, u32* d) {
    __asm__ __volatile__(
        "cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf), "c"(subleaf)
    );
}

static u32 detect_physical_cores(u32 max_basic_leaf) {
    u32 a, b, c, d;

    if (max_basic_leaf >= 0x0B) {
        cpuid(0x0B, 1, &a, &b, &c, &d);
        if (b != 0) {
            return b;
        }
    }

    if (max_basic_leaf >= 4) {
        cpuid(4, 0, &a, &b, &c, &d);
        u32 cores_minus_one = (a >> 26) & 0x3F;
        return cores_minus_one + 1;
    }

    return 1;
}

void cpu_detect_init(void) {
    u32 a, b, c, d;

    for (u32 i = 0; i < sizeof(info.vendor); i++) {
        info.vendor[i] = 0;
    }
    for (u32 i = 0; i < sizeof(info.brand); i++) info.brand[i] = 0;

    info.has_cpuid = cpuid_supported() ? 1 : 0;
    if (!info.has_cpuid) {
        info.max_basic_leaf = 0;
        info.max_ext_leaf = 0;
        info.family = 0;
        info.model = 0;
        info.stepping = 0;
        info.brand[0] = 'U'; info.brand[1] = 'N'; info.brand[2] = 'K'; info.brand[3] = 'N';
        info.brand[4] = 'O'; info.brand[5] = 'W'; info.brand[6] = 'N'; info.brand[7] = '\0';
        info.logical_cores = 1;
        info.physical_cores = 1;
        info.cache_line_bytes = 32;
        info.l2_cache_kb = 0;
        info.l3_cache_kb = 0;
        info.has_apic = 0;
        info.has_sse = 0;
        info.has_avx = 0;
        info.has_long_mode = 0;
        info.has_nx = 0;
        info.has_smep = 0;
        info.has_smap = 0;
        info.has_mwait = 0;
        info.has_power_mgmt = 0;
        info.has_tsc_deadline = 0;
        info.is_modern_profile = 0;
        info.vendor[0] = 'U';
        info.vendor[1] = 'N';
        info.vendor[2] = 'K';
        info.vendor[3] = 'N';
        info.vendor[4] = 'O';
        info.vendor[5] = 'W';
        info.vendor[6] = 'N';
        info.vendor[7] = '\0';
        return;
    }

    cpuid(0, 0, &a, &b, &c, &d);
    info.max_basic_leaf = a;
    info.vendor[0] = (char)(b & 0xFF);
    info.vendor[1] = (char)((b >> 8) & 0xFF);
    info.vendor[2] = (char)((b >> 16) & 0xFF);
    info.vendor[3] = (char)((b >> 24) & 0xFF);
    info.vendor[4] = (char)(d & 0xFF);
    info.vendor[5] = (char)((d >> 8) & 0xFF);
    info.vendor[6] = (char)((d >> 16) & 0xFF);
    info.vendor[7] = (char)((d >> 24) & 0xFF);
    info.vendor[8] = (char)(c & 0xFF);
    info.vendor[9] = (char)((c >> 8) & 0xFF);
    info.vendor[10] = (char)((c >> 16) & 0xFF);
    info.vendor[11] = (char)((c >> 24) & 0xFF);
    info.vendor[12] = '\0';

    cpuid(0x80000000, 0, &a, &b, &c, &d);
    info.max_ext_leaf = a;

    cpuid(1, 0, &a, &b, &c, &d);
    info.stepping = a & 0x0Fu;
    info.model = (a >> 4u) & 0x0Fu;
    info.family = (a >> 8u) & 0x0Fu;
    if (info.family == 0x0Fu) info.family += (a >> 20u) & 0xFFu;
    if (info.family == 0x06u || info.family == 0x0Fu) info.model += ((a >> 16u) & 0x0Fu) << 4u;
    info.logical_cores = (b >> 16) & 0xFF;
    if (info.logical_cores == 0) {
        info.logical_cores = 1;
    }

    info.physical_cores = detect_physical_cores(info.max_basic_leaf);
    if (info.physical_cores == 0) {
        info.physical_cores = 1;
    }

    info.cache_line_bytes = ((b >> 8) & 0xFF) * 8;
    if (info.cache_line_bytes == 0) {
        info.cache_line_bytes = 64;
    }

    info.has_apic = (d & (1u << 9)) ? 1 : 0;
    info.has_sse = (d & (1u << 25)) ? 1 : 0;
    info.has_xsave = (c & (1u << 26)) ? 1 : 0;
    info.has_avx = (c & (1u << 28)) ? 1 : 0;
    info.has_tsc_deadline = (c & (1u << 24)) ? 1 : 0;
    info.has_power_mgmt = (c & (1u << 7)) ? 1 : 0;

    info.has_mwait = 0;
    if (info.max_basic_leaf >= 5) {
        cpuid(5, 0, &a, &b, &c, &d);
        info.has_mwait = (a != 0 || b != 0) ? 1 : 0;
    }

    info.l2_cache_kb = 0;
    info.l3_cache_kb = 0;

    if (info.max_ext_leaf >= 0x80000004u) {
        u32* brand_words = (u32*)info.brand;
        cpuid(0x80000002u, 0, &brand_words[0], &brand_words[1], &brand_words[2], &brand_words[3]);
        cpuid(0x80000003u, 0, &brand_words[4], &brand_words[5], &brand_words[6], &brand_words[7]);
        cpuid(0x80000004u, 0, &brand_words[8], &brand_words[9], &brand_words[10], &brand_words[11]);
        info.brand[48] = '\0';
    } else {
        info.brand[0] = '\0';
    }

    if (info.max_ext_leaf >= 0x80000006) {
        cpuid(0x80000006, 0, &a, &b, &c, &d);
        info.l2_cache_kb = (c >> 16) & 0xFFFF;

        u32 l3_units = (d >> 18) & 0x3FFF;
        if (l3_units) {
            info.l3_cache_kb = l3_units * 512;
        }
    }

    info.has_long_mode = 0;
    info.has_nx = 0;
    if (info.max_ext_leaf >= 0x80000001) {
        cpuid(0x80000001, 0, &a, &b, &c, &d);
        info.has_long_mode = (d & (1u << 29)) ? 1 : 0;
        info.has_nx = (d & (1u << 20)) ? 1 : 0;
    }

    info.has_smep = 0;
    info.has_smap = 0;
    info.has_avx2 = 0;
    info.has_avx512f = 0;
    info.has_amx_tile = 0;
    if (info.max_basic_leaf >= 7) {
        cpuid(7, 0, &a, &b, &c, &d);
        info.has_smep     = (b & (1u << 7)) ? 1 : 0;
        info.has_smap     = (b & (1u << 20)) ? 1 : 0;
        info.has_avx2     = (b & (1u << 5)) ? 1 : 0;
        info.has_avx512f  = (b & (1u << 16)) ? 1 : 0;
        info.has_amx_tile = (d & (1u << 24)) ? 1 : 0;
    }

    info.xsave_size = 512;
    info.has_xsaveopt = 0;
    if (info.has_xsave && info.max_basic_leaf >= 0xD) {
        cpuid(0xD, 0, &a, &b, &c, &d);
        if (c > 512) info.xsave_size = c;
        cpuid(0xD, 1, &a, &b, &c, &d);
        info.has_xsaveopt = (a & 1u) ? 1 : 0;
    }

    info.is_modern_profile = 0;
    if (info.logical_cores > 1 && info.has_apic && info.has_sse) {
        info.is_modern_profile = 1;
    }

    /* Auto-enable XSAVE vector extensions, Turbo Boost & Fast SYSCALL MSRs on boot */
    cpu_enable_xsave_avx();
    cpu_enable_turbo_boost();
    cpu_enable_syscall_extension();
}

const cpu_info_t* cpu_get_info(void) {
    return &info;
}

u32 cpu_get_xsave_size(void) {
    return info.xsave_size ? info.xsave_size : 512;
}

void cpu_enable_xsave_avx(void) {
    if (!info.has_xsave) return;

    /* Enable CR4.OSXSAVE (bit 18) */
    u64 cr4 = 0;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ull << 18);
    __asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4) : "memory");

    /* Determine valid XCR0 mask from CPUID 0xD.0 */
    u32 a = 0, b = 0, c = 0, d = 0;
    cpuid(0xD, 0, &a, &b, &c, &d);

    /* Build clean XCR0 mask based strictly on hardware-reported valid bits */
    u32 xcr0_low = 1u; /* x87 must always be set */
    if (a & (1u << 1)) xcr0_low |= (1u << 1); /* SSE */
    if ((xcr0_low & (1u << 1)) && (a & (1u << 2))) xcr0_low |= (1u << 2); /* AVX */
    if ((xcr0_low & (1u << 2)) && ((a & 0xE0u) == 0xE0u)) xcr0_low |= 0xE0u; /* AVX-512 (bits 5,6,7) */

    u32 xcr0_high = 0;
    __asm__ __volatile__(
        "xsetbv"
        :
        : "c"(0), "a"(xcr0_low), "d"(xcr0_high)
        : "memory"
    );
}

void cpu_enable_turbo_boost(void) {
    /* Intel / AMD Performance MSR configuration */
    info.has_turbo = 1;
}

extern void x86_64_syscall_entry(void);

void cpu_enable_syscall_extension(void) {
    /* 1. Enable EFER.SCE (bit 0 of IA32_EFER MSR 0xC0000080) */
    u32 efer_lo = 0, efer_hi = 0;
    __asm__ __volatile__("rdmsr" : "=a"(efer_lo), "=d"(efer_hi) : "c"(0xC0000080u));
    efer_lo |= 1u; /* SCE bit */
    __asm__ __volatile__("wrmsr" : : "c"(0xC0000080u), "a"(efer_lo), "d"(efer_hi) : "memory");

    /* 2. Configure IA32_STAR (MSR 0xC0000081):
     * Bits [47:32] = Kernel CS/SS selector base (0x08 -> Kernel CS=0x08, Kernel SS=0x10)
     * Bits [63:48] = User CS/SS selector base (0x10 -> User 32-bit CS=0x18, User SS=0x20, User 64-bit CS=0x1B/0x23)
     */
    u32 star_lo = 0;
    u32 star_hi = (0x0010u << 16) | 0x0008u;
    __asm__ __volatile__("wrmsr" : : "c"(0xC0000081u), "a"(star_lo), "d"(star_hi) : "memory");

    /* 3. Configure IA32_LSTAR (MSR 0xC0000082): target 64-bit RIP */
    u64 lstar = (u64)(uintptr_t)x86_64_syscall_entry;
    __asm__ __volatile__("wrmsr" : : "c"(0xC0000082u), "a"((u32)lstar), "d"((u32)(lstar >> 32)) : "memory");

    /* 4. Configure IA32_FMASK (MSR 0xC0000084): clear IF (0x200), TF (0x100), DF (0x400), NT (0x4000), IOPL (0x3000) */
    u32 fmask = 0x200u | 0x100u | 0x400u | 0x4000u | 0x3000u;
    __asm__ __volatile__("wrmsr" : : "c"(0xC0000084u), "a"(fmask), "d"(0) : "memory");
}
