#include "smp.h"
#include "hw_topology.h"
#include "paging.h"
#include "string.h"
#include "apic.h"

#define AP_TRAMPOLINE 0x8000u
#define AP_VECTOR 0x08u

extern u8 ap_trampoline_start[];
extern u8 ap_trampoline_end[];
extern u8 ap_trampoline_cr3[];
extern u8 ap_trampoline_stack[];
extern u8 ap_trampoline_entry[];

static smp_status_t g_smp;
static volatile u32 g_ap_online[HW_TOPO_MAX_CORES];
static u8 g_ap_stacks[HW_TOPO_MAX_CORES][4096] __attribute__((aligned(4096)));

static void spin_delay(void) {
    for (volatile u32 i = 0u; i < 100000u; i++) {
        __asm__ __volatile__("pause");
    }
}

void smp_init(void) {
    hw_topology_status_t hw;
    hw_topology_status(&hw);
    memset(&g_smp, 0, sizeof(g_smp));
    memset((void*)g_ap_online, 0, sizeof(g_ap_online));
    g_smp.detected = hw.core_count;
    g_ap_online[0] = 1u;
    g_smp.online = 1u;

    /* Get LAPIC address from APIC driver which properly parsed MADT */
    if (apic_is_available()) {
        g_smp.lapic_addr = apic_lapic_read(LAPIC_ID) ? 0xFEE00000u : 0u;
        /* Better: use known LAPIC address which is typically 0xFEE00000 */
        /* The apic_init() already has the real address from MADT */
        {
            apic_status_t st;
            apic_status(&st);
            if (st.lapic_addr) {
                g_smp.lapic_addr = st.lapic_addr;
            } else {
                g_smp.lapic_addr = (hw.madt_found && hw.lapic_addr) ? hw.lapic_addr : 0xFEE00000u;
            }
        }
    } else {
        g_smp.lapic_addr = (hw.madt_found && hw.lapic_addr) ? hw.lapic_addr : 0u;
    }
}

void smp_ap_entry(void) {
    u32 id = apic_lapic_id();
    for (u32 i = 1u; i < g_smp.detected && i < HW_TOPO_MAX_CORES; i++) {
        if (hw_topology_core_apic_id(i) == id) {
            g_ap_online[i] = 1u;
            break;
        }
    }
    apic_lapic_eoi();
    for (;;) __asm__ __volatile__("hlt");
}

int smp_start_aps(void) {
    if (g_smp.detected <= 1u || g_smp.lapic_addr == 0u) return 0;
    g_smp.started = 0u;

    /* Enable LAPIC on BSP (already done by apic_init) */
    if (apic_is_available()) {
        apic_lapic_enable();
    }

    u32 size = (u32)(ap_trampoline_end - ap_trampoline_start);
    if (size > 4096u) return -1;
    memcpy((void*)AP_TRAMPOLINE, ap_trampoline_start, size);
    u32 base = AP_TRAMPOLINE;
    *(u32*)(base + ((u32)ap_trampoline_cr3 - (u32)ap_trampoline_start)) = paging_cr3();
    *(u32*)(base + ((u32)ap_trampoline_entry - (u32)ap_trampoline_start)) = (u32)smp_ap_entry;

    u32 bsp = apic_lapic_id();
    for (u32 i = 1u; i < g_smp.detected && i < HW_TOPO_MAX_CORES; i++) {
        u32 apic = hw_topology_core_apic_id(i);
        if (apic == bsp || apic > 255u) continue;
        *(u32*)(base + ((u32)ap_trampoline_stack - (u32)ap_trampoline_start)) =
            (u32)&g_ap_stacks[i][sizeof(g_ap_stacks[i]) - 16u];
        
        /* Use our new APIC IPI functions */
        apic_send_init_ipi(apic);
        spin_delay();
        apic_send_startup_ipi(apic, AP_VECTOR);

        for (u32 wait = 0u; wait < 100u; wait++) {
            if (g_ap_online[i]) break;
            spin_delay();
        }
        if (g_ap_online[i]) g_smp.started++;
    }
    g_smp.online = 1u;
    for (u32 i = 1u; i < g_smp.detected && i < HW_TOPO_MAX_CORES; i++) if (g_ap_online[i]) g_smp.online++;
    return (int)g_smp.started;
}

u32 smp_online_count(void) { return g_smp.online ? g_smp.online : 1u; }

void smp_status(smp_status_t* out) { if (out) *out = g_smp; }
