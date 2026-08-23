use core::cmp::Ordering;

const MAX_TASKS: usize = 128;
const MAX_CORES: usize = 64;

#[repr(C)]
#[derive(Copy, Clone)]
pub struct RustSchedSnapshot {
    pub initialized: u32,
    pub task_count: u32,
    pub ready_count: u32,
    pub last_pick_task: u32,
    pub core_count: u32,
    pub rebalance_events: u32,
}

#[derive(Copy, Clone)]
struct TaskEntry {
    task_id: u32,
    priority: u8,
    class_id: u8,
    vruntime: u64,
    recent_runtime: u32,
    pinned_core: u8,
    thermal_class: u8,
    latency_target: u8,
    flow_id: u8,
    cache_heat: u32,
    burst_score: u32,
    pressure: u32,
    wait_ticks: u32,
    wake_predict: u32,
    migrations: u32,
    present: bool,
    age: u16,
}

impl TaskEntry {
    const fn empty() -> Self {
        Self {
            task_id: 0,
            priority: 39,
            class_id: 0,
            vruntime: 0,
            recent_runtime: 0,
            pinned_core: 0,
            thermal_class: 0,
            latency_target: 0,
            flow_id: 0,
            cache_heat: 0,
            burst_score: 0,
            pressure: 0,
            wait_ticks: 0,
            wake_predict: 0,
            migrations: 0,
            present: false,
            age: 0,
        }
    }

    fn score(&self, pick_core: u32) -> u64 {
        let mut s = self.vruntime;
        let prio_boost = (40u64.saturating_sub(self.priority as u64)) * 4u64;
        s = s.saturating_sub(prio_boost);

        if self.class_id == 2 {
            s = s.saturating_sub(24);
        } else if self.class_id == 1 {
            s = s.saturating_sub(12);
        }

        if self.pinned_core as u32 == pick_core {
            s = s.saturating_sub(16 + (self.cache_heat.min(64) as u64 / 4));
        } else if self.cache_heat > 75 {
            s = s.saturating_add(18);
        }

        if self.age > 0 {
            s = s.saturating_sub((self.age as u64) / 2);
        }
        if self.latency_target >= 2 {
            s = s.saturating_sub(8 + (self.wake_predict.min(80) as u64 / 5));
        }
        if self.burst_score > 40 {
            s = s.saturating_sub(12);
        }
        if self.pressure > 80 {
            s = s.saturating_add(22);
        }
        if self.thermal_class >= 2 && self.cache_heat > 70 {
            s = s.saturating_add(16);
        }
        s
    }
}

pub struct SchedulerCore {
    tasks: [TaskEntry; MAX_TASKS],
    core_load: [u32; MAX_CORES],
    core_temperature: [u32; MAX_CORES],
    core_queue_temp: [u32; MAX_CORES],
    core_idle_shadow: [u32; MAX_CORES],
    core_last_task: [u32; MAX_CORES],
    initialized: bool,
    task_count: u32,
    ready_count: u32,
    core_count: u32,
    rr_cursor: u32,
    last_pick_task: u32,
    rebalance_events: u32,
    tick_counter: u64,
}

impl SchedulerCore {
    pub const fn new() -> Self {
        Self {
            tasks: [TaskEntry::empty(); MAX_TASKS],
            core_load: [0; MAX_CORES],
            core_temperature: [0; MAX_CORES],
            core_queue_temp: [0; MAX_CORES],
            core_idle_shadow: [1; MAX_CORES],
            core_last_task: [0; MAX_CORES],
            initialized: false,
            task_count: 0,
            ready_count: 0,
            core_count: 1,
            rr_cursor: 0,
            last_pick_task: 0,
            rebalance_events: 0,
            tick_counter: 0,
        }
    }

    pub fn init(&mut self, cores: u32) {
        self.tasks = [TaskEntry::empty(); MAX_TASKS];
        self.core_load = [0; MAX_CORES];
        self.core_temperature = [0; MAX_CORES];
        self.core_queue_temp = [0; MAX_CORES];
        self.core_idle_shadow = [1; MAX_CORES];
        self.core_last_task = [0; MAX_CORES];
        self.initialized = true;
        self.task_count = 0;
        self.ready_count = 0;
        self.core_count = if cores == 0 {
            1
        } else {
            cores.min(MAX_CORES as u32)
        };
        self.rr_cursor = 0;
        self.last_pick_task = 0;
        self.rebalance_events = 0;
        self.tick_counter = 0;
    }

    fn find_or_alloc_task(&mut self, task_id: u32) -> Option<&mut TaskEntry> {
        let mut found_idx = None;
        let mut free_idx = None;
        for (i, t) in self.tasks.iter().enumerate() {
            if t.present && t.task_id == task_id {
                found_idx = Some(i);
                break;
            }
            if !t.present && free_idx.is_none() {
                free_idx = Some(i);
            }
        }
        if let Some(idx) = found_idx {
            return Some(&mut self.tasks[idx]);
        }
        if let Some(idx) = free_idx {
            let t = &mut self.tasks[idx];
            *t = TaskEntry::empty();
            t.present = true;
            t.task_id = task_id;
            self.task_count = self.task_count.saturating_add(1);
            self.ready_count = self.ready_count.saturating_add(1);
            return Some(t);
        }
        None
    }

    pub fn enqueue(
        &mut self,
        task_id: u32,
        priority: u8,
        task_class: u8,
        vruntime_hint: u32,
        recent_runtime: u32,
    ) {
        if !self.initialized {
            return;
        }
        let core_count = self.core_count;
        if let Some(t) = self.find_or_alloc_task(task_id) {
            t.priority = priority.min(39);
            t.class_id = task_class.min(2);
            t.recent_runtime = recent_runtime;
            t.vruntime = t.vruntime.saturating_add(vruntime_hint as u64 / 2);
            if core_count > 1 {
                t.pinned_core = (task_id % core_count) as u8;
            }
        }
    }

    pub fn update_signals(
        &mut self,
        task_id: u32,
        home_core: u32,
        cache_heat: u32,
        burst_score: u32,
        pressure: u32,
        wait_ticks: u32,
        latency_target: u32,
        thermal_class: u32,
        flow_id: u32,
        wake_predict: u32,
    ) {
        if !self.initialized {
            return;
        }
        let core_count = self.core_count.max(1);
        if let Some(t) = self.find_or_alloc_task(task_id) {
            let new_core = (home_core % core_count) as u8;
            if t.pinned_core != new_core {
                t.migrations = t.migrations.saturating_add(1);
            }
            t.pinned_core = new_core;
            t.cache_heat = cache_heat.min(100);
            t.burst_score = burst_score.min(100);
            t.pressure = pressure.min(100);
            t.wait_ticks = wait_ticks.min(1000);
            t.latency_target = latency_target.min(3) as u8;
            t.thermal_class = thermal_class.min(3) as u8;
            t.flow_id = flow_id.min(255) as u8;
            t.wake_predict = wake_predict.min(100);
        }
    }

    pub fn tick(&mut self, core_id: u32, elapsed_ticks: u32) {
        if !self.initialized {
            return;
        }
        let c = (core_id % self.core_count.max(1)) as usize;
        self.core_load[c] = self.core_load[c].saturating_add(elapsed_ticks);
        self.core_temperature[c] =
            (self.core_temperature[c] + elapsed_ticks.saturating_mul(2)).min(100);
        self.core_queue_temp[c] = (self.core_queue_temp[c] + self.ready_count.min(16)).min(100);
        self.core_idle_shadow[c] = 0;
        self.tick_counter = self.tick_counter.saturating_add(elapsed_ticks as u64);

        for t in self.tasks.iter_mut() {
            if !t.present {
                continue;
            }
            if t.recent_runtime > 0 {
                t.recent_runtime = t.recent_runtime.saturating_sub(elapsed_ticks);
            }
            t.vruntime = t.vruntime.saturating_add((elapsed_ticks as u64) * 2u64);
            t.age = t.age.saturating_add(1);
            if t.cache_heat > 0 {
                t.cache_heat -= 1;
            }
        }

        for i in 0..(self.core_count as usize) {
            if i != c {
                self.core_temperature[i] = self.core_temperature[i].saturating_sub(1);
                self.core_queue_temp[i] = self.core_queue_temp[i].saturating_sub(2);
                if self.core_load[i] == 0 {
                    self.core_idle_shadow[i] = 1;
                }
            }
            self.core_load[i] = (self.core_load[i] * 7) / 8;
        }

        if self.tick_counter % 128 == 0 {
            self.rebalance();
        }
    }

    fn rebalance(&mut self) {
        if self.core_count <= 1 {
            return;
        }
        let mut min_core = 0usize;
        let mut max_core = 0usize;
        for i in 1..(self.core_count as usize) {
            if self.core_load[i] < self.core_load[min_core] {
                min_core = i;
            }
            if self.core_load[i] > self.core_load[max_core] {
                max_core = i;
            }
        }
        if self.core_load[max_core] <= self.core_load[min_core] + 16 {
            return;
        }

        for t in self.tasks.iter_mut() {
            if !t.present {
                continue;
            }
            if t.pinned_core as usize == max_core && t.class_id != 2 {
                t.pinned_core = min_core as u8;
                self.rebalance_events = self.rebalance_events.saturating_add(1);
                break;
            }
        }
        self.core_load[min_core] = self.core_load[min_core].saturating_add(8);
        self.core_load[max_core] = self.core_load[max_core].saturating_sub(8);
    }

    fn core_score(&self, task: TaskEntry, core: usize) -> u32 {
        let mut score = self.core_load[core]
            .saturating_add(self.core_temperature[core])
            .saturating_add(self.core_queue_temp[core]);
        if task.pinned_core as usize == core {
            score = score.saturating_sub(task.cache_heat / 4);
        }
        if self.core_last_task[core] == task.task_id {
            score = score.saturating_sub(12);
        }
        if task.latency_target >= 2 && self.core_idle_shadow[core] != 0 {
            score = score.saturating_sub(20);
        }
        if task.thermal_class >= 2 {
            score = score.saturating_add(self.core_temperature[core] / 2);
        }
        score
    }

    pub fn pick_core(&mut self, task_id: u32, fallback_core: u32) -> Option<u32> {
        if !self.initialized || self.core_count == 0 {
            return None;
        }
        let task = *self.find_task(task_id)?;
        let mut best = (fallback_core % self.core_count) as usize;
        let mut best_score = self.core_score(task, best);
        for core in 0..(self.core_count as usize) {
            let score = self.core_score(task, core);
            if score < best_score {
                best = core;
                best_score = score;
            }
        }
        self.core_last_task[best] = task_id;
        Some(best as u32)
    }

    pub fn pick_next(&mut self, core_id: u32) -> Option<u32> {
        if !self.initialized || self.task_count == 0 {
            return None;
        }

        let mut best_idx: Option<usize> = None;
        let mut best_score: u64 = u64::MAX;

        /* Phase 1: deterministic best-score scan with rr tie-break. */
        for offset in 0..MAX_TASKS {
            let i = ((self.rr_cursor as usize) + offset) % MAX_TASKS;
            let t = self.tasks[i];
            if !t.present {
                continue;
            }
            let s = t.score(core_id);
            match s.cmp(&best_score) {
                Ordering::Less => {
                    best_score = s;
                    best_idx = Some(i);
                }
                Ordering::Equal => {
                    if let Some(bi) = best_idx {
                        if self.tasks[i].age > self.tasks[bi].age {
                            best_idx = Some(i);
                        }
                    }
                }
                Ordering::Greater => {}
            }
        }

        let idx = best_idx?;
        self.rr_cursor = (idx as u32 + 1) % (MAX_TASKS as u32);
        self.tasks[idx].age = 0;
        self.tasks[idx].vruntime = self.tasks[idx].vruntime.saturating_add(4);
        self.last_pick_task = self.tasks[idx].task_id;
        Some(self.tasks[idx].task_id)
    }

    fn find_task(&self, task_id: u32) -> Option<&TaskEntry> {
        for t in self.tasks.iter() {
            if t.present && t.task_id == task_id {
                return Some(t);
            }
        }
        None
    }

    pub fn advise(
        &self,
        task_id: u32,
        core_id: u32,
        current_ns: u32,
        runqueue_depth: u32,
        pressure_hint: u32,
        runtime_ticks: u32,
    ) -> i32 {
        let mut delta: i32 = 0;
        let task = match self.find_task(task_id) {
            Some(t) => t,
            None => return 0,
        };

        if task.pinned_core as u32 == core_id {
            delta -= 4;
        } else {
            delta += 2;
        }

        if task.class_id == 2 {
            delta -= 6;
        } else if task.class_id == 1 {
            delta -= 3;
        } else if runqueue_depth > 8 {
            delta += 2;
        }

        if task.priority <= 8 {
            delta -= 6;
        } else if task.priority >= 30 {
            delta += 4;
        }

        if pressure_hint >= 3 {
            if task.class_id == 2 {
                delta -= 4;
            } else {
                delta += 2;
            }
        } else if pressure_hint == 0 && task.class_id == 0 {
            delta -= 1;
        }

        /* Cheap locality signal until namespace metadata is explicitly mirrored in Rust. */
        if current_ns != 0 && (task.task_id & 0x3) == (current_ns & 0x3) {
            delta -= 2;
        }

        if runtime_ticks > 1000 {
            delta += 3;
        }

        if delta < -32 {
            -32
        } else if delta > 32 {
            32
        } else {
            delta
        }
    }

    pub fn snapshot(&self) -> RustSchedSnapshot {
        RustSchedSnapshot {
            initialized: if self.initialized { 1 } else { 0 },
            task_count: self.task_count,
            ready_count: self.ready_count,
            last_pick_task: self.last_pick_task,
            core_count: self.core_count,
            rebalance_events: self.rebalance_events,
        }
    }
}
