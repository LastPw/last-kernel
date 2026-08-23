#include "shm.h"
#include "serial.h"
#include "string.h"
#include "frame_allocator.h"

static shm_region_t g_shm_regions[16];
static u8 g_shm_initialized = 0;

int shm_init(void) {
    memset(g_shm_regions, 0, sizeof(g_shm_regions));
    g_shm_initialized = 1;
    serial_writeln("[SHM] POSIX Shared Memory IPC Engine Initialized");
    return 0;
}

int shm_open(const char* name, u32 size, u64* out_phys) {
    if (!g_shm_initialized || !name || !out_phys || size == 0) return -1;
    for (u32 i = 0; i < 16; i++) {
        if (g_shm_regions[i].active && str_cmp(g_shm_regions[i].name, name) == 0) {
            g_shm_regions[i].ref_count++;
            *out_phys = g_shm_regions[i].phys_addr;
            return 0;
        }
    }
    for (u32 i = 0; i < 16; i++) {
        if (!g_shm_regions[i].active) {
            void* frame = frame_alloc();
            if (!frame) return -1;
            str_cpy(g_shm_regions[i].name, name);
            g_shm_regions[i].phys_addr = (u64)(uintptr_t)frame;
            g_shm_regions[i].size_bytes = size;
            g_shm_regions[i].ref_count = 1;
            g_shm_regions[i].active = 1;
            *out_phys = g_shm_regions[i].phys_addr;
            return 0;
        }
    }
    return -1;
}

int shm_close(const char* name) {
    if (!g_shm_initialized || !name) return -1;
    for (u32 i = 0; i < 16; i++) {
        if (g_shm_regions[i].active && str_cmp(g_shm_regions[i].name, name) == 0) {
            if (--g_shm_regions[i].ref_count == 0) {
                g_shm_regions[i].active = 0;
            }
            return 0;
        }
    }
    return -1;
}
