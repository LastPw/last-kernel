//! Security Guard - Memory-Safe Security Policy Monitor in Rust for LastOS
//!
//! Enforces process capability limits, tracks security violation metrics,
//! and evaluates security policy decisions safely without dynamic allocations.

pub const MAX_MONITORED_TASKS: usize = 128;

#[repr(C)]
#[derive(Copy, Clone)]
pub struct TaskSecurityProfile {
    pub task_id: u32,
    pub capabilities: u32,
    pub threat_score: u16,
    pub syscall_burst_count: u16,
    pub violation_count: u16,
    pub is_quarantined: u8,
    pub is_active: u8,
}

impl TaskSecurityProfile {
    pub const fn empty() -> Self {
        Self {
            task_id: 0,
            capabilities: 0,
            threat_score: 0,
            syscall_burst_count: 0,
            violation_count: 0,
            is_quarantined: 0,
            is_active: 0,
        }
    }
}

pub struct SecurityGuard {
    tasks: [TaskSecurityProfile; MAX_MONITORED_TASKS],
    global_lockdown_level: u8,
}

impl SecurityGuard {
    pub const fn new() -> Self {
        Self {
            tasks: [TaskSecurityProfile::empty(); MAX_MONITORED_TASKS],
            global_lockdown_level: 0,
        }
    }

    pub fn register_task(&mut self, task_id: u32, caps: u32) -> bool {
        if task_id == 0 {
            return false;
        }
        let slot = (task_id as usize) % MAX_MONITORED_TASKS;
        self.tasks[slot] = TaskSecurityProfile {
            task_id,
            capabilities: caps,
            threat_score: 0,
            syscall_burst_count: 0,
            violation_count: 0,
            is_quarantined: 0,
            is_active: 1,
        };
        true
    }

    pub fn has_capability(&self, task_id: u32, cap: u32) -> bool {
        if task_id == 0 {
            return true; // Kernel task 0 has all caps
        }
        let slot = (task_id as usize) % MAX_MONITORED_TASKS;
        let profile = &self.tasks[slot];
        if profile.is_active == 1 && profile.task_id == task_id {
            if profile.is_quarantined == 1 {
                return false; // Quarantined processes lose extra privileges
            }
            (profile.capabilities & cap) == cap
        } else {
            false
        }
    }

    pub fn record_violation(&mut self, task_id: u32, severity_weight: u16) {
        if task_id == 0 {
            return;
        }
        let slot = (task_id as usize) % MAX_MONITORED_TASKS;
        let profile = &mut self.tasks[slot];
        if profile.is_active == 1 {
            profile.violation_count = profile.violation_count.saturating_add(1);
            profile.threat_score = profile.threat_score.saturating_add(severity_weight);

            // Quarantine task if threat score reaches threshold (e.g. 500)
            if profile.threat_score >= 500 {
                profile.is_quarantined = 1;
            }
        }
    }

    pub fn set_lockdown(&mut self, level: u8) {
        self.global_lockdown_level = level;
    }

    pub fn lockdown_level(&self) -> u8 {
        self.global_lockdown_level
    }
}
