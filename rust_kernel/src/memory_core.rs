//! Memory Core - Monolithic Memory Safety & Telemetry in Rust for LastOS
//!
//! Provides zero-cost x86_64 canonical address validation, user-space range guards,
//! memory pressure computation, and fragmentation telemetry without dynamic allocation.

pub const USER_SPACE_TOP: u64 = 0x00007FFFFFFFFFFFu64;
pub const KERNEL_SPACE_BASE: u64 = 0xFFFF800000000000u64;

#[repr(C)]
#[derive(Copy, Clone)]
pub struct RustMemoryStats {
    pub total_ram_bytes: u64,
    pub allocated_bytes: u64,
    pub peak_allocated_bytes: u64,
    pub allocation_count: u32,
    pub free_count: u32,
    pub pressure_score: u32,
    pub fragmentation_ratio: u32,
}

pub struct MemoryCore {
    total_ram_bytes: u64,
    allocated_bytes: u64,
    peak_allocated_bytes: u64,
    alloc_count: u32,
    free_count: u32,
    initialized: bool,
}

impl MemoryCore {
    pub const fn new() -> Self {
        Self {
            total_ram_bytes: 0,
            allocated_bytes: 0,
            peak_allocated_bytes: 0,
            alloc_count: 0,
            free_count: 0,
            initialized: false,
        }
    }

    pub fn init(&mut self, total_ram: u64) {
        self.total_ram_bytes = total_ram;
        self.allocated_bytes = 0;
        self.peak_allocated_bytes = 0;
        self.alloc_count = 0;
        self.free_count = 0;
        self.initialized = true;
    }

    /// Validates whether a user-space address range is strictly within user canonical bounds
    /// and does not overlap with kernel space or wrap around.
    pub fn validate_user_range(&self, addr: u64, len: u64, _is_write: bool) -> bool {
        if len == 0 {
            return true;
        }

        // Check for arithmetic overflow
        let end = match addr.checked_add(len) {
            Some(e) => e,
            None => return false,
        };

        // Address must be in user space (below USER_SPACE_TOP)
        if addr > USER_SPACE_TOP || end > USER_SPACE_TOP {
            return false;
        }

        // Null pointer dereference protection for low 4KB
        if addr < 0x1000 {
            return false;
        }

        true
    }

    /// Record a memory allocation
    pub fn on_alloc(&mut self, size: u64) {
        self.alloc_count = self.alloc_count.saturating_add(1);
        self.allocated_bytes = self.allocated_bytes.saturating_add(size);
        if self.allocated_bytes > self.peak_allocated_bytes {
            self.peak_allocated_bytes = self.allocated_bytes;
        }
    }

    /// Record a memory free
    pub fn on_free(&mut self, size: u64) {
        self.free_count = self.free_count.saturating_add(1);
        self.allocated_bytes = self.allocated_bytes.saturating_sub(size);
    }

    /// Computes memory pressure score (0 = light, 100 = critical OOM)
    pub fn pressure_score(&self) -> u32 {
        if self.total_ram_bytes == 0 {
            return 0;
        }
        let pct = (self.allocated_bytes * 100) / self.total_ram_bytes;
        pct.min(100) as u32
    }

    /// Returns memory snapshot
    pub fn snapshot(&self) -> RustMemoryStats {
        RustMemoryStats {
            total_ram_bytes: self.total_ram_bytes,
            allocated_bytes: self.allocated_bytes,
            peak_allocated_bytes: self.peak_allocated_bytes,
            allocation_count: self.alloc_count,
            free_count: self.free_count,
            pressure_score: self.pressure_score(),
            fragmentation_ratio: if self.alloc_count > 0 {
                (self.free_count * 100) / self.alloc_count
            } else {
                0
            },
        }
    }
}
