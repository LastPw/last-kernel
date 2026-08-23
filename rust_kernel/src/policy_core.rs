const IPC_TRACK: usize = 64;
const COMPAT_TRACK: usize = 64;

#[derive(Copy, Clone)]
struct IpcTrack {
    used: bool,
    src_task: u32,
    score_ema: u32,
    rejects_ema: u32,
    last_action: u32,
}

impl IpcTrack {
    const fn empty() -> Self {
        Self {
            used: false,
            src_task: 0,
            score_ema: 0,
            rejects_ema: 0,
            last_action: 0,
        }
    }
}

#[derive(Copy, Clone)]
struct CompatTrack {
    used: bool,
    task_id: u32,
    risk_ema: u32,
    fail_streak: u32,
    last_action: u32,
    last_level: u32,
}

impl CompatTrack {
    const fn empty() -> Self {
        Self {
            used: false,
            task_id: 0,
            risk_ema: 0,
            fail_streak: 0,
            last_action: 0,
            last_level: 0,
        }
    }
}

pub struct PolicyCore {
    ipc: [IpcTrack; IPC_TRACK],
    compat: [CompatTrack; COMPAT_TRACK],
}

impl PolicyCore {
    pub const fn new() -> Self {
        Self {
            ipc: [IpcTrack::empty(); IPC_TRACK],
            compat: [CompatTrack::empty(); COMPAT_TRACK],
        }
    }

    pub fn init(&mut self) {
        self.ipc = [IpcTrack::empty(); IPC_TRACK];
        self.compat = [CompatTrack::empty(); COMPAT_TRACK];
    }

    fn clamp100(v: u32) -> u32 {
        if v > 100 {
            100
        } else {
            v
        }
    }

    fn ipc_slot(&mut self, src_task: u32) -> usize {
        for i in 0..IPC_TRACK {
            if self.ipc[i].used && self.ipc[i].src_task == src_task {
                return i;
            }
        }
        for i in 0..IPC_TRACK {
            if !self.ipc[i].used {
                self.ipc[i].used = true;
                self.ipc[i].src_task = src_task;
                self.ipc[i].score_ema = 0;
                self.ipc[i].rejects_ema = 0;
                self.ipc[i].last_action = 0;
                return i;
            }
        }
        (src_task as usize) % IPC_TRACK
    }

    pub fn ipc_decide(
        &mut self,
        src_task: u32,
        _dst_task: u32,
        same_ns: u32,
        queue_depth: u32,
        lockdown: u32,
        rate_window_count: u32,
    ) -> (u32, u32) {
        let idx = self.ipc_slot(src_task);
        let mut risk = 0u32;

        risk += (queue_depth.saturating_mul(100)) / 16;
        risk += (rate_window_count.saturating_mul(100)) / 24;
        risk += lockdown.saturating_mul(14);

        if same_ns == 0 {
            risk += 22;
        }

        let prev = self.ipc[idx].score_ema;
        let smooth = (prev.saturating_mul(7) + risk.saturating_mul(3)) / 10;
        self.ipc[idx].score_ema = Self::clamp100(smooth);

        let mut action = 0u32; // allow
        if self.ipc[idx].score_ema >= 85 {
            action = 2; // block
            self.ipc[idx].rejects_ema = Self::clamp100(self.ipc[idx].rejects_ema.saturating_add(8));
        } else if self.ipc[idx].score_ema >= 60 {
            action = 1; // throttle
            self.ipc[idx].rejects_ema = Self::clamp100(self.ipc[idx].rejects_ema.saturating_add(3));
        } else if self.ipc[idx].rejects_ema > 0 {
            self.ipc[idx].rejects_ema -= 1;
        }

        if lockdown >= 3 && same_ns == 0 {
            action = 2;
        }

        self.ipc[idx].last_action = action;
        (action, self.ipc[idx].score_ema)
    }

    fn compat_slot(&mut self, task_id: u32) -> usize {
        for i in 0..COMPAT_TRACK {
            if self.compat[i].used && self.compat[i].task_id == task_id {
                return i;
            }
        }
        for i in 0..COMPAT_TRACK {
            if !self.compat[i].used {
                self.compat[i].used = true;
                self.compat[i].task_id = task_id;
                self.compat[i].risk_ema = 0;
                self.compat[i].fail_streak = 0;
                self.compat[i].last_action = 0;
                self.compat[i].last_level = 0;
                return i;
            }
        }
        (task_id as usize) % COMPAT_TRACK
    }

    pub fn compat_guard(
        &mut self,
        task_id: u32,
        sync_score: u32,
        dll_sync: u32,
        drift: u32,
        bridge_conf: u32,
        sece_trust: u32,
        lockdown: u32,
        burst_hit: u32,
        failures: u32,
        risky_nr: u32,
    ) -> (u32, u32, u32) {
        let idx = self.compat_slot(task_id);
        let mut risk = 0u32;

        if sync_score < 70 {
            risk += 70 - sync_score;
        }
        if dll_sync < 65 {
            risk += 65 - dll_sync;
        }
        if bridge_conf < 65 {
            risk += 65 - bridge_conf;
        }
        if sece_trust < 70 {
            risk += 70 - sece_trust;
        }

        risk += drift / 2;
        risk += failures.saturating_mul(5);
        risk += lockdown.saturating_mul(10);
        if burst_hit != 0 {
            risk += 18;
        }
        if risky_nr != 0 {
            risk += 12;
        }

        let smooth = (self.compat[idx].risk_ema.saturating_mul(8) + risk.saturating_mul(2)) / 10;
        self.compat[idx].risk_ema = Self::clamp100(smooth);

        if failures > 0 {
            self.compat[idx].fail_streak = self.compat[idx].fail_streak.saturating_add(1);
        } else if self.compat[idx].fail_streak > 0 {
            self.compat[idx].fail_streak -= 1;
        }

        let level = if self.compat[idx].risk_ema >= 85 {
            3
        } else if self.compat[idx].risk_ema >= 60 {
            2
        } else if self.compat[idx].risk_ema >= 35 {
            1
        } else {
            0
        };

        let mut action = 0u32; // allow
        if level >= 3 || (lockdown >= 3 && risky_nr != 0) {
            action = 2; // block
        } else if level >= 2 || self.compat[idx].fail_streak >= 3 {
            action = 1; // fallback
        }

        let penalty = if action == 2 {
            18
        } else if action == 1 {
            8
        } else {
            0
        };

        self.compat[idx].last_action = action;
        self.compat[idx].last_level = level;
        (action, level, penalty)
    }
}

