const MAX_CACHE: usize = 256;
const MAX_PATH: usize = 96;

#[repr(C)]
#[derive(Copy, Clone)]
pub struct RustVfsCacheEntry {
    pub valid: u32,
    pub node_id: u32,
    pub flags: u8,
    pub _reserved: [u8; 3],
    pub size: u32,
    pub last_tick_hint: u32,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct RustVfsCacheStats {
    pub hits: u32,
    pub misses: u32,
    pub evictions: u32,
    pub entries: u32,
    pub capacity: u32,
}

#[derive(Copy, Clone)]
struct CacheNode {
    used: bool,
    hash: u32,
    path: [u8; MAX_PATH],
    path_len: u8,
    node_id: u32,
    flags: u8,
    size: u32,
    tick: u32,
}

impl CacheNode {
    const fn empty() -> Self {
        Self {
            used: false,
            hash: 0,
            path: [0; MAX_PATH],
            path_len: 0,
            node_id: 0,
            flags: 0,
            size: 0,
            tick: 0,
        }
    }
}

pub struct VfsCache {
    nodes: [CacheNode; MAX_CACHE],
    cap: usize,
    entries: u32,
    clock: u32,
    hits: u32,
    misses: u32,
    evictions: u32,
}

impl VfsCache {
    pub const fn new() -> Self {
        Self {
            nodes: [CacheNode::empty(); MAX_CACHE],
            cap: MAX_CACHE,
            entries: 0,
            clock: 1,
            hits: 0,
            misses: 0,
            evictions: 0,
        }
    }

    pub fn init(&mut self, capacity: usize) {
        self.nodes = [CacheNode::empty(); MAX_CACHE];
        self.cap = capacity.max(8).min(MAX_CACHE);
        self.entries = 0;
        self.clock = 1;
        self.hits = 0;
        self.misses = 0;
        self.evictions = 0;
    }

    fn hash_key(path: &[u8]) -> u32 {
        let mut h: u32 = 2166136261;
        for &b in path {
            h ^= b as u32;
            h = h.wrapping_mul(16777619);
        }
        h
    }

    fn path_eq(a: &CacheNode, path: &[u8]) -> bool {
        if a.path_len as usize != path.len() {
            return false;
        }
        let plen = path.len();
        let mut i = 0usize;
        while i < plen {
            if a.path[i] != path[i] {
                return false;
            }
            i += 1;
        }
        true
    }

    fn find_index(&self, path: &[u8], hash: u32) -> Option<usize> {
        let mut i = 0usize;
        while i < self.cap {
            let n = self.nodes[i];
            if n.used && n.hash == hash && Self::path_eq(&n, path) {
                return Some(i);
            }
            i += 1;
        }
        None
    }

    fn choose_victim(&self) -> usize {
        let mut victim = 0usize;
        let mut oldest = u32::MAX;
        let mut i = 0usize;
        while i < self.cap {
            let n = self.nodes[i];
            if !n.used {
                return i;
            }
            if n.tick < oldest {
                oldest = n.tick;
                victim = i;
            }
            i += 1;
        }
        victim
    }

    pub fn put(&mut self, path: &[u8], node_id: u32, flags: u8, size: u32) {
        if path.is_empty() {
            return;
        }
        let plen = path.len().min(MAX_PATH - 1);
        let hash = Self::hash_key(&path[..plen]);
        self.clock = self.clock.wrapping_add(1);

        if let Some(i) = self.find_index(&path[..plen], hash) {
            self.nodes[i].node_id = node_id;
            self.nodes[i].flags = flags;
            self.nodes[i].size = size;
            self.nodes[i].tick = self.clock;
            return;
        }

        let idx = self.choose_victim();
        if self.nodes[idx].used {
            self.evictions = self.evictions.saturating_add(1);
        } else {
            self.entries = self.entries.saturating_add(1);
        }
        let n = &mut self.nodes[idx];
        n.used = true;
        n.hash = hash;
        n.path_len = plen as u8;
        n.path = [0; MAX_PATH];
        let mut i = 0usize;
        while i < plen {
            n.path[i] = path[i];
            i += 1;
        }
        n.node_id = node_id;
        n.flags = flags;
        n.size = size;
        n.tick = self.clock;
    }

    pub fn get(&mut self, path: &[u8]) -> Option<RustVfsCacheEntry> {
        if path.is_empty() {
            self.misses = self.misses.saturating_add(1);
            return None;
        }
        let plen = path.len().min(MAX_PATH - 1);
        let hash = Self::hash_key(&path[..plen]);
        self.clock = self.clock.wrapping_add(1);

        if let Some(i) = self.find_index(&path[..plen], hash) {
            let n = &mut self.nodes[i];
            n.tick = self.clock;
            self.hits = self.hits.saturating_add(1);
            return Some(RustVfsCacheEntry {
                valid: 1,
                node_id: n.node_id,
                flags: n.flags,
                _reserved: [0; 3],
                size: n.size,
                last_tick_hint: n.tick,
            });
        }
        self.misses = self.misses.saturating_add(1);
        None
    }

    pub fn invalidate(&mut self, path: &[u8]) {
        if path.is_empty() {
            return;
        }
        let plen = path.len().min(MAX_PATH - 1);
        let hash = Self::hash_key(&path[..plen]);
        if let Some(i) = self.find_index(&path[..plen], hash) {
            self.nodes[i] = CacheNode::empty();
            if self.entries > 0 {
                self.entries -= 1;
            }
        }
    }

    pub fn stats(&self) -> RustVfsCacheStats {
        RustVfsCacheStats {
            hits: self.hits,
            misses: self.misses,
            evictions: self.evictions,
            entries: self.entries,
            capacity: self.cap as u32,
        }
    }
}

