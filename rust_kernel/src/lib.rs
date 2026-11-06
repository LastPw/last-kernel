#![no_std]
#![allow(static_mut_refs)]

mod path_guard;
mod policy_core;
mod scheduler_core;
mod security_guard;
mod vfs_cache;
mod memory_core;

use core::panic::PanicInfo;
use core::slice;
use path_guard::PathGuard;
use policy_core::PolicyCore;
use scheduler_core::{RustSchedSnapshot, SchedulerCore};
use security_guard::SecurityGuard;
use vfs_cache::{RustVfsCacheEntry, RustVfsCacheStats, VfsCache};
use memory_core::{MemoryCore, RustMemoryStats};

static mut SCHED: SchedulerCore = SchedulerCore::new();
static mut CACHE: VfsCache = VfsCache::new();
static mut POLICY: PolicyCore = PolicyCore::new();
static mut SEC_GUARD: SecurityGuard = SecurityGuard::new();
static mut MEMORY: MemoryCore = MemoryCore::new();

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Rust Scheduler Core FFI
 * ═══════════════════════════════════════════════════════════════════════ */

#[no_mangle]
pub extern "C" fn rust_sched_init(cores: u32) {
    unsafe { SCHED.init(cores) };
}

#[no_mangle]
pub extern "C" fn rust_sched_enqueue(
    task_id: u32,
    priority: u8,
    task_class: u8,
    vruntime_hint: u32,
    recent_runtime: u32,
) {
    unsafe {
        SCHED.enqueue(task_id, priority, task_class, vruntime_hint, recent_runtime);
    }
}

#[no_mangle]
pub extern "C" fn rust_sched_task_signals(
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
    unsafe {
        SCHED.update_signals(
            task_id,
            home_core,
            cache_heat,
            burst_score,
            pressure,
            wait_ticks,
            latency_target,
            thermal_class,
            flow_id,
            wake_predict,
        );
    }
}

#[no_mangle]
pub extern "C" fn rust_sched_tick(core_id: u32, elapsed_ticks: u32) {
    unsafe {
        SCHED.tick(core_id, elapsed_ticks);
    }
}

#[no_mangle]
pub extern "C" fn rust_sched_pick_core(
    task_id: u32,
    fallback_core: u32,
    out_core: *mut u32,
) -> i32 {
    if out_core.is_null() {
        return -1;
    }
    unsafe {
        if let Some(core) = SCHED.pick_core(task_id, fallback_core) {
            *out_core = core;
            0
        } else {
            *out_core = fallback_core;
            -1
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_sched_pick_next(core_id: u32, out_task_id: *mut u32) -> i32 {
    if out_task_id.is_null() {
        return -1;
    }
    unsafe {
        if let Some(task) = SCHED.pick_next(core_id) {
            *out_task_id = task;
            0
        } else {
            *out_task_id = 0;
            -1
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_sched_advise(
    task_id: u32,
    core_id: u32,
    current_ns: u32,
    runqueue_depth: u32,
    pressure_hint: u32,
    runtime_ticks: u32,
    out_delta: *mut i32,
) -> i32 {
    if out_delta.is_null() {
        return -1;
    }
    unsafe {
        *out_delta = SCHED.advise(
            task_id,
            core_id,
            current_ns,
            runqueue_depth,
            pressure_hint,
            runtime_ticks,
        );
    }
    0
}

#[no_mangle]
pub extern "C" fn rust_sched_snapshot(out: *mut RustSchedSnapshot) -> i32 {
    if out.is_null() {
        return -1;
    }
    unsafe {
        *out = SCHED.snapshot();
    }
    0
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Rust Memory Management Core FFI
 * ═══════════════════════════════════════════════════════════════════════ */

#[no_mangle]
pub extern "C" fn rust_mem_init(total_ram_bytes: u64) {
    unsafe {
        MEMORY.init(total_ram_bytes);
    }
}

#[no_mangle]
pub extern "C" fn rust_mem_validate_user_range(addr: u64, len: u64, is_write: u32) -> i32 {
    unsafe {
        if MEMORY.validate_user_range(addr, len, is_write != 0) {
            1
        } else {
            0
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_mem_on_alloc(size: u64) {
    unsafe {
        MEMORY.on_alloc(size);
    }
}

#[no_mangle]
pub extern "C" fn rust_mem_on_free(size: u64) {
    unsafe {
        MEMORY.on_free(size);
    }
}

#[no_mangle]
pub extern "C" fn rust_mem_pressure_score() -> u32 {
    unsafe { MEMORY.pressure_score() }
}

#[no_mangle]
pub extern "C" fn rust_mem_snapshot(out: *mut RustMemoryStats) -> i32 {
    if out.is_null() {
        return -1;
    }
    unsafe {
        *out = MEMORY.snapshot();
    }
    0
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Rust VFS Cache FFI
 * ═══════════════════════════════════════════════════════════════════════ */

#[no_mangle]
pub extern "C" fn rust_vfs_cache_init(capacity: u32) {
    unsafe {
        CACHE.init(capacity as usize);
    }
}

#[no_mangle]
pub extern "C" fn rust_vfs_cache_put(
    path: *const u8,
    path_len: u32,
    node_id: u32,
    flags: u8,
    size: u32,
) -> i32 {
    if path.is_null() || path_len == 0 {
        return -1;
    }
    let bytes = unsafe { slice::from_raw_parts(path, path_len as usize) };
    unsafe {
        CACHE.put(bytes, node_id, flags, size);
    }
    0
}

#[no_mangle]
pub extern "C" fn rust_vfs_cache_get(
    path: *const u8,
    path_len: u32,
    out: *mut RustVfsCacheEntry,
) -> i32 {
    if path.is_null() || out.is_null() || path_len == 0 {
        return -1;
    }
    let bytes = unsafe { slice::from_raw_parts(path, path_len as usize) };
    unsafe {
        match CACHE.get(bytes) {
            Some(e) => {
                *out = e;
                0
            }
            None => -1,
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_vfs_cache_invalidate(path: *const u8, path_len: u32) -> i32 {
    if path.is_null() || path_len == 0 {
        return -1;
    }
    let bytes = unsafe { slice::from_raw_parts(path, path_len as usize) };
    unsafe {
        CACHE.invalidate(bytes);
    }
    0
}

#[no_mangle]
pub extern "C" fn rust_vfs_cache_stats(out: *mut RustVfsCacheStats) -> i32 {
    if out.is_null() {
        return -1;
    }
    unsafe {
        *out = CACHE.stats();
    }
    0
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Rust Policy Core FFI
 * ═══════════════════════════════════════════════════════════════════════ */

#[no_mangle]
pub extern "C" fn rust_policy_init() {
    unsafe {
        POLICY.init();
    }
}

#[no_mangle]
pub extern "C" fn rust_policy_ipc_decide(
    src_task: u32,
    dst_task: u32,
    same_ns: u32,
    queue_depth: u32,
    lockdown: u32,
    rate_window_count: u32,
    out_action: *mut u32,
    out_score: *mut u32,
) -> i32 {
    if out_action.is_null() || out_score.is_null() {
        return -1;
    }
    let (action, score) = unsafe {
        POLICY.ipc_decide(
            src_task,
            dst_task,
            same_ns,
            queue_depth,
            lockdown,
            rate_window_count,
        )
    };
    unsafe {
        *out_action = action;
        *out_score = score;
    }
    0
}

#[no_mangle]
pub extern "C" fn rust_path_canonicalize(
    in_ptr: *const u8,
    in_len: usize,
    out_ptr: *mut u8,
    out_max: usize,
) -> i32 {
    if in_ptr.is_null() || out_ptr.is_null() || in_len == 0 || out_max < 2 {
        return -1;
    }
    let input = unsafe { slice::from_raw_parts(in_ptr, in_len) };
    let out_buf = unsafe { slice::from_raw_parts_mut(out_ptr, out_max) };

    match PathGuard::canonicalize(input, out_buf) {
        Ok(len) => len as i32,
        Err(_) => -1,
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Rust Security Guard FFI
 * ═══════════════════════════════════════════════════════════════════════ */

#[no_mangle]
pub extern "C" fn rust_sec_guard_register(task_id: u32, caps: u32) -> i32 {
    unsafe {
        if SEC_GUARD.register_task(task_id, caps) {
            0
        } else {
            -1
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_sec_guard_check_cap(task_id: u32, cap: u32) -> i32 {
    unsafe {
        if SEC_GUARD.has_capability(task_id, cap) {
            1
        } else {
            0
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_sec_guard_record_violation(task_id: u32, weight: u16) {
    unsafe {
        SEC_GUARD.record_violation(task_id, weight);
    }
}
