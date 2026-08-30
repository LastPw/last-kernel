#include "numa.h"
#include "string.h"
#include "spinlock.h"

/* ==============================================================================
 * NUMA & Multi-Socket Topology Engine
 * ============================================================================== */

static numa_topology_t s_numa;
static spinlock_t s_numa_lock;

void numa_init(void) {
    spinlock_init(&s_numa_lock);
    u32 flags = spinlock_lock_irqsave(&s_numa_lock);

    s_numa.numa_enabled = 1;
    s_numa.node_count = 0;
    s_numa.total_cpus = 0;
    s_numa.default_policy = NUMA_POLICY_LOCAL_FIRST;

    for (u32 i = 0; i < MAX_NUMA_NODES; i++) {
        s_numa.nodes[i].node_id = i;
        s_numa.nodes[i].online = 0;
        s_numa.nodes[i].memory_base_phys = 0;
        s_numa.nodes[i].memory_size_bytes = 0;
        s_numa.nodes[i].free_memory_bytes = 0;
        s_numa.nodes[i].cpu_count = 0;
        s_numa.nodes[i].total_allocations = 0;
        s_numa.nodes[i].local_hits = 0;
        s_numa.nodes[i].remote_fallbacks = 0;

        for (u32 j = 0; j < MAX_NUMA_NODES; j++) {
            s_numa.nodes[i].distance[j] = (i == j) ? NUMA_DISTANCE_LOCAL : NUMA_DISTANCE_REMOTE;
        }
    }

    /* Initialize Default Dual-Socket 2-Node Server Topology */
    numa_register_node(0, 0x100000000ULL, 32ULL * 1024 * 1024 * 1024); /* Node 0: 32 GB */
    numa_register_node(1, 0x900000000ULL, 32ULL * 1024 * 1024 * 1024); /* Node 1: 32 GB */

    /* Assign Socket 0 CPUs (0..15) and Socket 1 CPUs (16..31) */
    for (u32 c = 0; c < 16; c++) numa_add_cpu_to_node(0, c);
    for (u32 c = 16; c < 32; c++) numa_add_cpu_to_node(1, c);

    spinlock_unlock_irqrestore(&s_numa_lock, flags);
}

int numa_register_node(u32 node_id, u64 base_phys, u64 size_bytes) {
    if (node_id >= MAX_NUMA_NODES) return -1;

    s_numa.nodes[node_id].node_id = node_id;
    s_numa.nodes[node_id].online = 1;
    s_numa.nodes[node_id].memory_base_phys = base_phys;
    s_numa.nodes[node_id].memory_size_bytes = size_bytes;
    s_numa.nodes[node_id].free_memory_bytes = size_bytes;

    if (node_id >= s_numa.node_count) {
        s_numa.node_count = node_id + 1;
    }
    return 0;
}

int numa_add_cpu_to_node(u32 node_id, u32 cpu_id) {
    if (node_id >= MAX_NUMA_NODES || !s_numa.nodes[node_id].online) return -1;
    if (s_numa.nodes[node_id].cpu_count >= MAX_CPUS_PER_NODE) return -2;

    u32 idx = s_numa.nodes[node_id].cpu_count++;
    s_numa.nodes[node_id].cpu_ids[idx] = cpu_id;
    s_numa.total_cpus++;
    return 0;
}

int numa_set_distance(u32 node_a, u32 node_b, u8 distance) {
    if (node_a >= MAX_NUMA_NODES || node_b >= MAX_NUMA_NODES) return -1;
    s_numa.nodes[node_a].distance[node_b] = distance;
    s_numa.nodes[node_b].distance[node_a] = distance;
    return 0;
}

u32 numa_get_current_node(void) {
    /* Default to Node 0 in single-core or early boot */
    return 0;
}

u32 numa_get_cpu_node(u32 cpu_id) {
    for (u32 i = 0; i < s_numa.node_count; i++) {
        if (!s_numa.nodes[i].online) continue;
        for (u32 c = 0; c < s_numa.nodes[i].cpu_count; c++) {
            if (s_numa.nodes[i].cpu_ids[c] == cpu_id) return i;
        }
    }
    return 0;
}

u64 numa_alloc_pages(u32 target_node, u32 order, numa_policy_t policy) {
    u32 flags = spinlock_lock_irqsave(&s_numa_lock);
    u64 page_size = 4096ULL << order;

    u32 selected_node = target_node;
    if (selected_node >= s_numa.node_count || !s_numa.nodes[selected_node].online) {
        selected_node = 0;
    }

    if (policy == NUMA_POLICY_LOCAL_FIRST || policy == NUMA_POLICY_PREFERRED) {
        if (s_numa.nodes[selected_node].free_memory_bytes >= page_size) {
            s_numa.nodes[selected_node].free_memory_bytes -= page_size;
            s_numa.nodes[selected_node].total_allocations++;
            s_numa.nodes[selected_node].local_hits++;
            u64 addr = s_numa.nodes[selected_node].memory_base_phys + (s_numa.nodes[selected_node].total_allocations * page_size);
            spinlock_unlock_irqrestore(&s_numa_lock, flags);
            return addr;
        }
        /* Fallback to nearest remote node */
        for (u32 n = 0; n < s_numa.node_count; n++) {
            if (n != selected_node && s_numa.nodes[n].online && s_numa.nodes[n].free_memory_bytes >= page_size) {
                s_numa.nodes[n].free_memory_bytes -= page_size;
                s_numa.nodes[n].total_allocations++;
                s_numa.nodes[selected_node].remote_fallbacks++;
                u64 addr = s_numa.nodes[n].memory_base_phys + (s_numa.nodes[n].total_allocations * page_size);
                spinlock_unlock_irqrestore(&s_numa_lock, flags);
                return addr;
            }
        }
    } else {
        /* Generic round-robin */
        s_numa.nodes[selected_node].free_memory_bytes -= page_size;
        s_numa.nodes[selected_node].total_allocations++;
        s_numa.nodes[selected_node].local_hits++;
        u64 addr = s_numa.nodes[selected_node].memory_base_phys + (s_numa.nodes[selected_node].total_allocations * page_size);
        spinlock_unlock_irqrestore(&s_numa_lock, flags);
        return addr;
    }

    spinlock_unlock_irqrestore(&s_numa_lock, flags);
    return 0;
}

void numa_free_pages(u64 phys_addr, u32 order) {
    u32 flags = spinlock_lock_irqsave(&s_numa_lock);
    u64 page_size = 4096ULL << order;

    for (u32 i = 0; i < s_numa.node_count; i++) {
        if (s_numa.nodes[i].online &&
            phys_addr >= s_numa.nodes[i].memory_base_phys &&
            phys_addr < (s_numa.nodes[i].memory_base_phys + s_numa.nodes[i].memory_size_bytes)) {
            s_numa.nodes[i].free_memory_bytes += page_size;
            break;
        }
    }
    spinlock_unlock_irqrestore(&s_numa_lock, flags);
}

void numa_get_topology(numa_topology_t* out) {
    if (!out) return;
    u32 flags = spinlock_lock_irqsave(&s_numa_lock);
    *out = s_numa;
    spinlock_unlock_irqrestore(&s_numa_lock, flags);
}

u8 numa_is_enabled(void) {
    return s_numa.numa_enabled;
}
