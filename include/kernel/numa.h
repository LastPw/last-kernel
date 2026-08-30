#ifndef NUMA_H
#define NUMA_H

#include "types.h"

#define MAX_NUMA_NODES          8
#define MAX_CPUS_PER_NODE       64
#define NUMA_DISTANCE_LOCAL     10
#define NUMA_DISTANCE_REMOTE    21

typedef enum {
    NUMA_POLICY_LOCAL_FIRST = 0,    /* Allocate on current CPU's local NUMA node */
    NUMA_POLICY_INTERLEAVED = 1,    /* Round-robin across all available NUMA nodes */
    NUMA_POLICY_BIND        = 2,    /* Strictly bind to a specific NUMA node */
    NUMA_POLICY_PREFERRED   = 3     /* Prefer specified node, fallback to nearest */
} numa_policy_t;

typedef struct {
    u32 node_id;
    u8  online;
    u64 memory_base_phys;
    u64 memory_size_bytes;
    u64 free_memory_bytes;
    u32 cpu_count;
    u32 cpu_ids[MAX_CPUS_PER_NODE];
    u8  distance[MAX_NUMA_NODES];
    u64 total_allocations;
    u64 local_hits;
    u64 remote_fallbacks;
} numa_node_t;

typedef struct {
    u8          numa_enabled;
    u32         node_count;
    u32         total_cpus;
    numa_policy_t default_policy;
    numa_node_t nodes[MAX_NUMA_NODES];
} numa_topology_t;

/* NUMA Subsystem API */
void numa_init(void);
int numa_register_node(u32 node_id, u64 base_phys, u64 size_bytes);
int numa_add_cpu_to_node(u32 node_id, u32 cpu_id);
int numa_set_distance(u32 node_a, u32 node_b, u8 distance);

u32 numa_get_current_node(void);
u32 numa_get_cpu_node(u32 cpu_id);
u64 numa_alloc_pages(u32 node_id, u32 order, numa_policy_t policy);
void numa_free_pages(u64 phys_addr, u32 order);

void numa_get_topology(numa_topology_t* out);
u8 numa_is_enabled(void);

#endif /* NUMA_H */
