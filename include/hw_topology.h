#ifndef HW_TOPOLOGY_H
#define HW_TOPOLOGY_H

#include "types.h"
#include "cpu_detect.h"

#define HW_TOPO_MAX_CORES 256u
#define HW_TOPO_MAX_NUMA 8u

typedef struct {
    u32 acpi_found;
    u32 madt_found;
    u32 srat_found;
    u32 fadt_found;
    u32 mcfg_found;
    u32 lapic_addr;
    u32 core_count;
    u32 numa_nodes;
    u32 thermal_sensor;
} hw_topology_status_t;

void hw_topology_init(const cpu_info_t* cpu);
u32 hw_topology_core_count(void);
u32 hw_topology_numa_nodes(void);
u32 hw_topology_core_apic_id(u32 core);
u32 hw_topology_core_numa_node(u32 core);
u32 hw_topology_core_temperature(u32 core);
u32 hw_topology_has_thermal_sensor(void);
void hw_topology_account_core(u32 core, u32 busy, u32 queue_depth);
void hw_topology_status(hw_topology_status_t* out);
u32 hw_topology_madt_addr(void);
u32 hw_topology_fadt_addr(void);
u32 hw_topology_mcfg_addr(void);
u32 hw_topology_hpet_addr(void);
u32 hw_topology_hpet_base(void);
const void* hw_topology_madt_virt(void);

/* ACPI power management helpers */
u32  hw_topology_pm1a_cnt_blk(void);
u32  hw_topology_pm1b_cnt_blk(void);
u32  hw_topology_pm1a_evt_blk(void);
u32  hw_topology_pm1b_evt_blk(void);
u32  hw_topology_pm_tmr_blk(void);
u16  hw_topology_slp_typa(void);
u16  hw_topology_slp_typb(void);
u16  hw_topology_smi_cmd(void);
u8   hw_topology_acpi_enable(void);
u8   hw_topology_acpi_disable(void);

/* PCIe MMCONFIG base */
u32  hw_topology_mcfg_base(void);
u32  hw_topology_mcfg_end_bus(void);

#endif
