#include "vfs.h"
#include "persistent_disk.h"
#include "disk.h"
#include "string.h"
#include "timer.h"
#include "rust_bridge.h"
#include "embedded_apps.h"

#define VFS_NAME_TOKEN 32u
#define VFS_PATH_TOKENS 16u
#define VFS_DISK_MAGIC 0x5346564Cu /* LVFS */
#define VFS_DISK_VERSION 2u
#define VFS_NODE_BLOCKS VFS_DATA_BLOCK_COUNT

typedef struct {
    u8 used;
    u8 locked;
    vfs_node_type_t type;
    u32 id;
    u32 parent_id;
    char name[VFS_MAX_NAME];
    u32 size;
    u32 created_tick;
    u32 modified_tick;
    u32 owner_uid;
    u32 owner_gid;
    u16 mode;
    u16 block_count;
    u16 blocks[VFS_NODE_BLOCKS];
} vfs_node_t;

static vfs_node_t g_nodes[VFS_MAX_NODES];
static u8 g_data_blocks[VFS_DATA_BLOCK_COUNT][VFS_DATA_BLOCK_SIZE];
static u8 g_block_used[VFS_DATA_BLOCK_COUNT];
static u32 g_next_id = 1u;
static vfs_backend_ops_t g_backend;
static u32 g_actor_uid = 0u;
static u32 g_actor_gid = 0u;

static vfs_journal_entry_t g_journal[VFS_JOURNAL_CAP];
static u32 g_journal_head = 0u;
static u32 g_journal_next_id = 1u;
static u8 g_loading_disk = 0u;
static u32 g_batch_depth = 0u;

/* These paths are the minimum boot and identity surface.  Their protection is
   independent of uid 0, so a compromised root shell cannot remove the floor
   from underneath the running system. */
static int vfs_is_critical_path(const char* path) {
    static const char* const protected_paths[] = {
        "/boot", "/boot/BOOT.BIN", "/boot/KERNEL.BIN", "/boot/lastos.conf",
        "/bin", "/bin/lastsh", "/sbin", "/lib", "/etc",
        "/etc/os-release", "/etc/hostname", "/etc/fstab", "/etc/passwd",
        "/etc/shadow", "/etc/profile", "/etc/motd", "/etc/hosts",
        "/etc/resolv.conf", "/etc/inittab", "/dev", "/proc", "/sys", "/root", 0
    };
    if (!path) return 0;
    for (u32 i = 0u; protected_paths[i]; i++) {
        if (str_cmp(path, protected_paths[i]) == 0) return 1;
    }
    return 0;
}

static int vfs_has_critical_path_loss(void) {
    static const char* const required_paths[] = {
        "/boot/lastos.conf", "/etc/os-release", "/etc/hostname",
        "/etc/fstab", "/etc/passwd", "/etc/shadow", "/etc/profile",
        "/etc/hosts", "/etc/resolv.conf", "/etc/inittab", 0
    };
    vfs_node_info_t info;
    for (u32 i = 0u; required_paths[i]; i++) {
        if (vfs_info(required_paths[i], &info) != 0 || !info.used ||
            info.type != VFS_NODE_FILE) return 1;
    }
    return 0;
}

typedef struct {
    u32 magic;
    u32 version;
    u32 node_size;
    u32 node_count;
    u32 next_id;
    u32 journal_head;
    u32 journal_next_id;
    u32 checksum;
} vfs_disk_header_t;

typedef struct {
    vfs_disk_header_t hdr;
    vfs_node_t nodes[VFS_MAX_NODES];
    u8 block_used[VFS_DATA_BLOCK_COUNT];
    u8 data_blocks[VFS_DATA_BLOCK_COUNT][VFS_DATA_BLOCK_SIZE];
} vfs_disk_image_t;

typedef char vfs_disk_image_must_fit_persistent_slot[
    sizeof(vfs_disk_image_t) <= (2043u * 512u) ? 1 : -1
];

static u32 vfs_disk_checksum(const vfs_disk_image_t* img) {
    u32 crc = 0xFFFFFFFFu;
    if (!img) return 0u;
    for (u32 i = 0u; i < sizeof(*img); i++) {
        u8 value = ((const u8*)img)[i];
        u32 checksum_start = (u32)((const u8*)&img->hdr.checksum - (const u8*)img);
        if (i >= checksum_start && i < checksum_start + sizeof(img->hdr.checksum)) value = 0u;
        crc ^= value;
        for (u8 bit = 0u; bit < 8u; bit++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}

static void vfs_clear_node_blocks(vfs_node_t* node) {
    if (!node) return;
    for (u32 i = 0u; i < node->block_count && i < VFS_NODE_BLOCKS; i++) {
        if (node->blocks[i] < VFS_DATA_BLOCK_COUNT) g_block_used[node->blocks[i]] = 0u;
        node->blocks[i] = 0u;
    }
    node->block_count = 0u;
}

static int vfs_add_block(vfs_node_t* node) {
    if (!node || node->block_count >= VFS_NODE_BLOCKS) return -1;
    for (u32 i = 0u; i < VFS_DATA_BLOCK_COUNT; i++) {
        if (!g_block_used[i]) {
            g_block_used[i] = 1u;
            node->blocks[node->block_count++] = (u16)i;
            memset(g_data_blocks[i], 0, VFS_DATA_BLOCK_SIZE);
            return 0;
        }
    }
    return -1;
}

static int vfs_write_range(vfs_node_t* node, u32 offset, const char* data, u32 len) {
    if (!node || (!data && len)) return -1;
    while (len) {
        u32 block_index = offset / VFS_DATA_BLOCK_SIZE;
        u32 block_offset = offset % VFS_DATA_BLOCK_SIZE;
        u32 take = VFS_DATA_BLOCK_SIZE - block_offset;
        if (block_index >= node->block_count || node->blocks[block_index] >= VFS_DATA_BLOCK_COUNT) return -1;
        if (take > len) take = len;
        memcpy(&g_data_blocks[node->blocks[block_index]][block_offset], data, take);
        offset += take;
        data += take;
        len -= take;
    }
    return 0;
}

static u32 vfs_read_range(const vfs_node_t* node, u32 offset, char* out, u32 len) {
    u32 copied = 0u;
    if (!node || !out) return 0u;
    while (len && offset < node->size) {
        u32 block_index = offset / VFS_DATA_BLOCK_SIZE;
        u32 block_offset = offset % VFS_DATA_BLOCK_SIZE;
        u32 take = VFS_DATA_BLOCK_SIZE - block_offset;
        if (take > len) take = len;
        if (take > node->size - offset) take = node->size - offset;
        if (block_index >= node->block_count || node->blocks[block_index] >= VFS_DATA_BLOCK_COUNT) break;
        memcpy(out + copied, &g_data_blocks[node->blocks[block_index]][block_offset], take);
        copied += take;
        offset += take;
        len -= take;
    }
    return copied;
}

static void vfs_flush_if_ready(void) {
    if (!g_loading_disk && g_batch_depth == 0u) {
        (void)vfs_flush_disk();
    }
}

void vfs_begin_batch(void) {
    g_batch_depth++;
}

int vfs_end_batch(void) {
    if (g_batch_depth == 0u) return -1;
    g_batch_depth--;
    if (g_batch_depth == 0u) return vfs_flush_disk();
    return 0;
}

static void cache_put_node_path(const char* path, const vfs_node_t* n) {
    if (!path || !n || !n->used) return;
    (void)rust_bridge_vfs_cache_put(path, n->id, (u8)n->type, n->size);
}

static void cache_invalidate_path(const char* path) {
    if (!path) return;
    rust_bridge_vfs_cache_invalidate(path);
}

static void path_parent_of(const char* path, char* out, u32 out_len) {
    u32 len = 0u;
    u32 cut = 0u;
    if (!out || out_len == 0u) return;
    out[0] = '/';
    if (out_len > 1u) out[1] = '\0';
    if (!path || path[0] != '/') return;

    len = str_len(path);
    if (len <= 1u) return;
    cut = len;
    while (cut > 1u && path[cut - 1u] == '/') cut--;
    while (cut > 1u && path[cut - 1u] != '/') cut--;
    if (cut <= 1u) return;
    if (cut >= out_len) cut = out_len - 1u;
    for (u32 i = 0u; i < cut; i++) out[i] = path[i];
    out[cut] = '\0';
}

static int index_by_id(u32 id) {
    for (u32 i = 0u; i < VFS_MAX_NODES; i++) {
        if (g_nodes[i].used && g_nodes[i].id == id) return (int)i;
    }
    return -1;
}

static void zero_name(char* n) {
    for (u32 i = 0; i < VFS_MAX_NAME; i++) n[i] = '\0';
}

static void set_name(char* dst, const char* src) {
    if (!dst) return;
    zero_name(dst);
    if (!src) return;
    str_ncpy(dst, src, VFS_MAX_NAME - 1u);
    dst[VFS_MAX_NAME - 1u] = '\0';
}

static int is_valid_name(const char* name) {
    if (!name || !name[0]) return 0;
    for (u32 i = 0; i < VFS_MAX_NAME - 1u && name[i]; i++) {
        char c = name[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*') return 0;
    }
    return 1;
}

static u16 perm_triplet(const vfs_node_t* n) {
    if (!n) return 0u;
    if (g_actor_uid == 0u) return 7u;
    if (g_actor_uid == n->owner_uid) return (u16)((n->mode >> 6u) & 0x7u);
    if (g_actor_gid == n->owner_gid) return (u16)((n->mode >> 3u) & 0x7u);
    return (u16)(n->mode & 0x7u);
}

static int has_access(const vfs_node_t* n, u8 access_mask) {
    u16 p = perm_triplet(n);
    if ((access_mask & VFS_ACC_READ) && ((p & VFS_PERM_R) == 0u)) return 0;
    if ((access_mask & VFS_ACC_WRITE) && ((p & VFS_PERM_W) == 0u)) return 0;
    if ((access_mask & VFS_ACC_EXEC) && ((p & VFS_PERM_X) == 0u)) return 0;
    return 1;
}

static void journal_push(vfs_journal_event_t ev, const vfs_node_t* n, u32 parent_id, u32 arg0, u32 arg1, const char* name) {
    u32 idx = g_journal_head % VFS_JOURNAL_CAP;
    g_journal[idx].id = g_journal_next_id++;
    g_journal[idx].tick = timer_ticks();
    g_journal[idx].event = ev;
    g_journal[idx].actor_uid = g_actor_uid;
    g_journal[idx].node_id = n ? n->id : 0u;
    g_journal[idx].parent_id = parent_id;
    g_journal[idx].arg0 = arg0;
    g_journal[idx].arg1 = arg1;
    set_name(g_journal[idx].name, name ? name : (n ? n->name : ""));
    g_journal_head++;
}

static int alloc_node(void) {
    for (u32 i = 0; i < VFS_MAX_NODES; i++) {
        if (!g_nodes[i].used) {
            g_nodes[i].used = 1u;
            g_nodes[i].id = g_next_id++;
            g_nodes[i].size = 0u;
            g_nodes[i].created_tick = timer_ticks();
            g_nodes[i].modified_tick = g_nodes[i].created_tick;
            g_nodes[i].locked = 0u;
            g_nodes[i].owner_uid = g_actor_uid;
            g_nodes[i].owner_gid = g_actor_gid;
            g_nodes[i].mode = 0u;
            g_nodes[i].block_count = 0u;
            for (u32 j = 0u; j < VFS_NODE_BLOCKS; j++) g_nodes[i].blocks[j] = 0u;
            zero_name(g_nodes[i].name);
            return (int)i;
        }
    }
    return -1;
}

static int root_index(void) {
    for (u32 i = 0; i < VFS_MAX_NODES; i++) {
        if (g_nodes[i].used && g_nodes[i].parent_id == 0u && str_cmp(g_nodes[i].name, "/") == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int find_child(u32 parent_id, const char* name) {
    for (u32 i = 0; i < VFS_MAX_NODES; i++) {
        if (!g_nodes[i].used) continue;
        if (g_nodes[i].parent_id != parent_id) continue;
        if (str_cmp(g_nodes[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int split_path(const char* path, char tokens[VFS_PATH_TOKENS][VFS_NAME_TOKEN], u32* out_count) {
    if (!path || !out_count) return -1;
    *out_count = 0u;
    if (path[0] != '/') return -1;

    u32 i = 1u;
    while (path[i] != '\0') {
        while (path[i] == '/') i++;
        if (path[i] == '\0') break;
        if (*out_count >= VFS_PATH_TOKENS) return -1;

        u32 k = 0u;
        while (path[i] != '\0' && path[i] != '/') {
            if (k >= VFS_NAME_TOKEN - 1u) return -1;
            tokens[*out_count][k++] = path[i++];
        }
        tokens[*out_count][k] = '\0';
        if (!is_valid_name(tokens[*out_count])) return -1;
        /* SECURITY: reject ".." to prevent path traversal attacks that
         * could bypass the critical-path protection list. */
        if (tokens[*out_count][0] == '.' && tokens[*out_count][1] == '.' && tokens[*out_count][2] == '\0') return -1;
        (*out_count)++;
    }
    return 0;
}

static int resolve_path(const char* path) {
    char canon_buf[256];
    if (!path) return -1;

    /* Use Rust memory-safe Path Guard if available to canonicalize path */
    if (rust_bridge_available()) {
        int rlen = rust_bridge_path_canonicalize(path, str_len(path), canon_buf, sizeof(canon_buf));
        if (rlen > 0) {
            path = canon_buf;
        }
    }

    if (str_cmp(path, "/") == 0) return root_index();

    {
        rust_vfs_cache_entry_t ce;
        if (rust_bridge_vfs_cache_get(path, &ce) == 0 && ce.valid) {
            int ci = index_by_id(ce.node_id);
            if (ci >= 0) {
                return ci;
            }
            rust_bridge_vfs_cache_invalidate(path);
        }
    }

    char tokens[VFS_PATH_TOKENS][VFS_NAME_TOKEN];
    u32 count = 0u;
    if (split_path(path, tokens, &count) != 0) return -1;

    int cur = root_index();
    if (cur < 0) return -1;
    for (u32 i = 0; i < count; i++) {
        if (!has_access(&g_nodes[cur], VFS_ACC_EXEC)) return -1;
        cur = find_child(g_nodes[cur].id, tokens[i]);
        if (cur < 0) return -1;
    }
    cache_put_node_path(path, &g_nodes[cur]);
    return cur;
}

static int resolve_parent(const char* path, char* leaf) {
    if (!path || !leaf || path[0] != '/') return -1;
    if (str_cmp(path, "/") == 0) return -1;

    char tokens[VFS_PATH_TOKENS][VFS_NAME_TOKEN];
    u32 count = 0u;
    if (split_path(path, tokens, &count) != 0 || count == 0u) return -1;

    set_name(leaf, tokens[count - 1u]);
    int cur = root_index();
    if (cur < 0) return -1;

    for (u32 i = 0; i + 1u < count; i++) {
        if (!has_access(&g_nodes[cur], VFS_ACC_EXEC)) return -1;
        int nxt = find_child(g_nodes[cur].id, tokens[i]);
        if (nxt < 0 || g_nodes[nxt].type != VFS_NODE_DIR) return -1;
        cur = nxt;
    }
    return cur;
}

void vfs_init(void) {
    for (u32 i = 0; i < VFS_MAX_NODES; i++) {
        g_nodes[i].used = 0u;
        g_nodes[i].locked = 0u;
        g_nodes[i].type = VFS_NODE_NONE;
        g_nodes[i].id = 0u;
        g_nodes[i].parent_id = 0u;
        g_nodes[i].size = 0u;
        g_nodes[i].created_tick = 0u;
        g_nodes[i].modified_tick = 0u;
        g_nodes[i].owner_uid = 0u;
        g_nodes[i].owner_gid = 0u;
        g_nodes[i].mode = 0u;
        zero_name(g_nodes[i].name);
        g_nodes[i].block_count = 0u;
        for (u32 j = 0u; j < VFS_NODE_BLOCKS; j++) g_nodes[i].blocks[j] = 0u;
    }
    memset(g_data_blocks, 0, sizeof(g_data_blocks));
    memset(g_block_used, 0, sizeof(g_block_used));
    for (u32 i = 0u; i < VFS_JOURNAL_CAP; i++) {
        g_journal[i].id = 0u;
        g_journal[i].tick = 0u;
        g_journal[i].event = VFS_J_NONE;
        g_journal[i].actor_uid = 0u;
        g_journal[i].node_id = 0u;
        g_journal[i].parent_id = 0u;
        g_journal[i].arg0 = 0u;
        g_journal[i].arg1 = 0u;
        zero_name(g_journal[i].name);
    }
    g_journal_head = 0u;
    g_journal_next_id = 1u;
    g_batch_depth = 0u;
    g_next_id = 1u;
    g_actor_uid = 0u;
    g_actor_gid = 0u;
    g_backend.node_created = 0;
    g_backend.node_deleted = 0;
    g_backend.file_written = 0;

    int r = alloc_node();
    if (r >= 0) {
        g_nodes[r].type = VFS_NODE_DIR;
        g_nodes[r].parent_id = 0u;
        g_nodes[r].owner_uid = 0u;
        g_nodes[r].owner_gid = 0u;
        g_nodes[r].mode = 0755u;
        set_name(g_nodes[r].name, "/");
        cache_put_node_path("/", &g_nodes[r]);

        /* Populate initial userland /bin applications */
        (void)vfs_mkdir("/bin");
        (void)vfs_create_file("/bin/uname");
        (void)vfs_write_file("/bin/uname", (const char*)BIN_UNAME_DATA, BIN_UNAME_LEN);
        (void)vfs_create_file("/bin/calc");
        (void)vfs_write_file("/bin/calc", (const char*)BIN_CALC_DATA, BIN_CALC_LEN);
        (void)vfs_create_file("/bin/cat");
        (void)vfs_write_file("/bin/cat", (const char*)BIN_CAT_DATA, BIN_CAT_LEN);
        (void)vfs_create_file("/bin/sh");
        (void)vfs_write_file("/bin/sh", (const char*)BIN_SH_DATA, BIN_SH_LEN);

        /* Populate initial /etc configuration files */
        (void)vfs_mkdir("/etc");
        (void)vfs_create_file("/etc/hostname");
        (void)vfs_write_file("/etc/hostname", "lastos\n", 7u);
        (void)vfs_create_file("/etc/os-release");
        (void)vfs_write_file("/etc/os-release", "NAME=\"LastOS\"\nVERSION=\"0.2.0\"\nID=lastos\nARCH=x86_64\n", 53u);
    }
}

void vfs_set_backend(const vfs_backend_ops_t* ops) {
    if (!ops) {
        g_backend.node_created = 0;
        g_backend.node_deleted = 0;
        g_backend.file_written = 0;
        return;
    }
    g_backend = *ops;
}

void vfs_load_disk(void) {
    if (!pdisk_ready()) return;

    static vfs_disk_image_t img;
    u32 len = 0u;
    if (pdisk_load_vfs_blob(&img, sizeof(img), &len) != 0) return;
    if (len == 0u) return;
    if (len != sizeof(img)) return;
    if (img.hdr.magic != VFS_DISK_MAGIC) return;
    if (img.hdr.version != VFS_DISK_VERSION) return;
    if (img.hdr.node_size != sizeof(vfs_node_t)) return;
    if (img.hdr.node_count != VFS_MAX_NODES) return;
    if (vfs_disk_checksum(&img) != img.hdr.checksum) return;

    g_loading_disk = 1u;
    memcpy(g_nodes, img.nodes, sizeof(g_nodes));
    memcpy(g_block_used, img.block_used, sizeof(g_block_used));
    memcpy(g_data_blocks, img.data_blocks, sizeof(g_data_blocks));
    g_next_id = img.hdr.next_id ? img.hdr.next_id : 1u;
    g_journal_head = img.hdr.journal_head;
    g_journal_next_id = img.hdr.journal_next_id ? img.hdr.journal_next_id : 1u;
    g_loading_disk = 0u;
    g_batch_depth = 0u;

    if (root_index() < 0) {
        vfs_init();
    }
}

int vfs_flush_disk(void) {
    if (!pdisk_ready()) return -1;

    static vfs_disk_image_t img;
    memset(&img, 0, sizeof(img));
    img.hdr.magic = VFS_DISK_MAGIC;
    img.hdr.version = VFS_DISK_VERSION;
    img.hdr.node_size = sizeof(vfs_node_t);
    img.hdr.node_count = VFS_MAX_NODES;
    img.hdr.next_id = g_next_id;
    img.hdr.journal_head = g_journal_head;
    img.hdr.journal_next_id = g_journal_next_id;
    memcpy(img.nodes, g_nodes, sizeof(g_nodes));
    memcpy(img.block_used, g_block_used, sizeof(g_block_used));
    memcpy(img.data_blocks, g_data_blocks, sizeof(g_data_blocks));
    img.hdr.checksum = vfs_disk_checksum(&img);
    if (pdisk_save_vfs_blob(&img, sizeof(img)) != 0) return -1;
    /* Complete the write-through path after the redundant snapshot is valid. */
    return disk_flush();
}

int vfs_rollback_disk(void) {
    if (pdisk_rollback_vfs() != 0) return -1;
    vfs_init();
    vfs_load_disk();
    return root_index() >= 0 ? 0 : -1;
}

int vfs_recover_critical(void) {
    if (!vfs_has_critical_path_loss()) return 0;
    /* Persistent-disk VFS uses two verified generations.  Roll back only for
       a missing boot/identity file; ordinary user data is never reconstructed
       or overwritten by this path. */
    if (vfs_rollback_disk() != 0 || vfs_has_critical_path_loss()) return -1;
    return 1;
}

int vfs_usage(u32* total_bytes, u32* used_bytes, u32* free_bytes) {
    u32 used = 0u;
    if (!total_bytes || !used_bytes || !free_bytes) return -1;
    for (u32 i = 0u; i < VFS_DATA_BLOCK_COUNT; i++) if (g_block_used[i]) used++;
    *total_bytes = VFS_DATA_BLOCK_COUNT * VFS_DATA_BLOCK_SIZE;
    *used_bytes = used * VFS_DATA_BLOCK_SIZE;
    *free_bytes = *total_bytes - *used_bytes;
    return 0;
}

int vfs_fsck(vfs_fsck_report_t* out, u8 repair) {
    u8 referenced[VFS_DATA_BLOCK_COUNT];
    u32 bad_nodes = 0u, bad_refs = 0u, refs = 0u, used = 0u, roots = 0u;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    memset(referenced, 0, sizeof(referenced));
    for (u32 i = 0u; i < VFS_MAX_NODES; i++) {
        vfs_node_t* n = &g_nodes[i];
        if (!n->used) continue;
        if (n->type == VFS_NODE_NONE || n->block_count > VFS_NODE_BLOCKS ||
            n->size > (u32)n->block_count * VFS_DATA_BLOCK_SIZE) { bad_nodes++; continue; }
        if (n->id == 0u) bad_nodes++;
        for (u32 k = i + 1u; k < VFS_MAX_NODES; k++) {
            if (g_nodes[k].used && g_nodes[k].id == n->id) { bad_nodes++; break; }
        }
        if (n->parent_id == 0u) {
            if (n->type != VFS_NODE_DIR || str_cmp(n->name, "/") != 0) bad_nodes++;
            roots++;
        } else {
            int parent = index_by_id(n->parent_id);
            if (parent < 0 || g_nodes[parent].type != VFS_NODE_DIR) {
                bad_nodes++;
            } else {
                u32 cursor = n->parent_id;
                for (u32 depth = 0u; depth < VFS_MAX_NODES && cursor != 0u; depth++) {
                    int ancestor = index_by_id(cursor);
                    if (ancestor < 0) { bad_nodes++; break; }
                    cursor = g_nodes[ancestor].parent_id;
                    if (cursor == n->id) { bad_nodes++; break; }
                }
            }
        }
        for (u32 j = 0u; j < n->block_count; j++) {
            u32 b = n->blocks[j];
            if (b >= VFS_DATA_BLOCK_COUNT || referenced[b]) { bad_refs++; continue; }
            referenced[b] = 1u; refs++;
        }
    }
    if (roots != 1u) bad_nodes += roots > 1u ? roots - 1u : 1u;
    for (u32 i = 0u; i < VFS_DATA_BLOCK_COUNT; i++) {
        if (g_block_used[i]) used++;
        if (g_block_used[i] != referenced[i]) {
            if (g_block_used[i]) out->orphan_blocks++;
            if (repair) g_block_used[i] = referenced[i];
        }
    }
    out->total_blocks = VFS_DATA_BLOCK_COUNT;
    out->used_blocks = used;
    out->referenced_blocks = refs;
    out->bad_nodes = bad_nodes;
    out->bad_references = bad_refs;
    out->journal_entries = g_journal_head;
    if (repair) (void)vfs_flush_disk();
    return (bad_nodes || bad_refs || out->orphan_blocks) ? -1 : 0;
}

void vfs_set_credentials(u32 uid, u32 gid) {
    g_actor_uid = uid;
    g_actor_gid = gid;
}

int vfs_mkdir(const char* path) {
    char leaf[VFS_MAX_NAME];
    int p = resolve_parent(path, leaf);
    if (p < 0 || g_nodes[p].type != VFS_NODE_DIR) return -1;
    if (!has_access(&g_nodes[p], VFS_ACC_WRITE | VFS_ACC_EXEC) || g_nodes[p].locked) return -1;
    if (find_child(g_nodes[p].id, leaf) >= 0) return -1;

    int n = alloc_node();
    if (n < 0) return -1;
    g_nodes[n].type = VFS_NODE_DIR;
    g_nodes[n].parent_id = g_nodes[p].id;
    g_nodes[n].mode = 0755u;
    set_name(g_nodes[n].name, leaf);
    g_nodes[n].modified_tick = timer_ticks();
    cache_put_node_path(path, &g_nodes[n]);
    {
        char parent[96];
        path_parent_of(path, parent, sizeof(parent));
        cache_invalidate_path(parent);
    }
    journal_push(VFS_J_CREATE, &g_nodes[n], g_nodes[p].id, VFS_NODE_DIR, 0u, leaf);
    if (g_backend.node_created) {
        vfs_node_info_t info;
        if (vfs_info(path, &info) == 0) {
            (void)g_backend.node_created(&info);
        }
    }
    vfs_flush_if_ready();
    return 0;
}

int vfs_create_file(const char* path) {
    char leaf[VFS_MAX_NAME];
    int p = resolve_parent(path, leaf);
    if (p < 0 || g_nodes[p].type != VFS_NODE_DIR) return -1;
    if (!has_access(&g_nodes[p], VFS_ACC_WRITE | VFS_ACC_EXEC) || g_nodes[p].locked) return -1;
    if (find_child(g_nodes[p].id, leaf) >= 0) return -1;

    int n = alloc_node();
    if (n < 0) return -1;
    g_nodes[n].type = VFS_NODE_FILE;
    g_nodes[n].parent_id = g_nodes[p].id;
    g_nodes[n].mode = 0644u;
    set_name(g_nodes[n].name, leaf);
    g_nodes[n].modified_tick = timer_ticks();
    cache_put_node_path(path, &g_nodes[n]);
    {
        char parent[96];
        path_parent_of(path, parent, sizeof(parent));
        cache_invalidate_path(parent);
    }
    journal_push(VFS_J_CREATE, &g_nodes[n], g_nodes[p].id, VFS_NODE_FILE, 0u, leaf);
    if (g_backend.node_created) {
        vfs_node_info_t info;
        if (vfs_info(path, &info) == 0) {
            (void)g_backend.node_created(&info);
        }
    }
    vfs_flush_if_ready();
    return 0;
}

int vfs_write_file(const char* path, const char* data, u32 len) {
    int n = resolve_path(path);
    if (n < 0 || g_nodes[n].type != VFS_NODE_FILE || g_nodes[n].locked) return -1;
    if (!has_access(&g_nodes[n], VFS_ACC_WRITE)) return -1;
    if (!data && len > 0u) return -1;
    if (len > VFS_FILE_LIMIT) return -1;

    vfs_clear_node_blocks(&g_nodes[n]);
    while (g_nodes[n].block_count * VFS_DATA_BLOCK_SIZE < len) {
        if (vfs_add_block(&g_nodes[n]) != 0) {
            vfs_clear_node_blocks(&g_nodes[n]);
            return -1;
        }
    }
    if (vfs_write_range(&g_nodes[n], 0u, data, len) != 0) return -1;
    g_nodes[n].size = len;
    g_nodes[n].modified_tick = timer_ticks();
    cache_put_node_path(path, &g_nodes[n]);
    journal_push(VFS_J_WRITE, &g_nodes[n], g_nodes[n].parent_id, len, 0u, g_nodes[n].name);
    /* Block-backed files are intentionally not exposed as a transient RAM pointer. */
    vfs_flush_if_ready();
    return 0;
}

int vfs_append_file(const char* path, const char* data, u32 len) {
    int n = resolve_path(path);
    u32 old_size;
    if (n < 0 || g_nodes[n].type != VFS_NODE_FILE || g_nodes[n].locked) return -1;
    if (!has_access(&g_nodes[n], VFS_ACC_WRITE) || (!data && len)) return -1;
    old_size = g_nodes[n].size;
    if (old_size > VFS_FILE_LIMIT || len > VFS_FILE_LIMIT - old_size) return -1;
    while (g_nodes[n].block_count * VFS_DATA_BLOCK_SIZE < old_size + len) {
        if (vfs_add_block(&g_nodes[n]) != 0) return -1;
    }
    if (vfs_write_range(&g_nodes[n], old_size, data, len) != 0) return -1;
    g_nodes[n].size = old_size + len;
    g_nodes[n].modified_tick = timer_ticks();
    cache_put_node_path(path, &g_nodes[n]);
    journal_push(VFS_J_WRITE, &g_nodes[n], g_nodes[n].parent_id, len, old_size, g_nodes[n].name);
    vfs_flush_if_ready();
    return 0;
}

int vfs_read_file(const char* path, char* out, u32 out_len) {
    if (!out || out_len == 0u) return -1;
    int n = resolve_path(path);
    if (n < 0 || g_nodes[n].type != VFS_NODE_FILE) return -1;
    if (!has_access(&g_nodes[n], VFS_ACC_READ)) return -1;

    u32 take = g_nodes[n].size;
    if (take >= out_len) take = out_len - 1u;
    take = vfs_read_range(&g_nodes[n], 0u, out, take);
    out[take] = '\0';
    cache_put_node_path(path, &g_nodes[n]);
    return (int)take;
}

int vfs_read_file_at(const char* path, u32 offset, char* out, u32 out_len) {
    int n;
    u32 take;
    if (!out || out_len == 0u) return -1;
    n = resolve_path(path);
    if (n < 0 || g_nodes[n].type != VFS_NODE_FILE || !has_access(&g_nodes[n], VFS_ACC_READ)) return -1;
    if (offset >= g_nodes[n].size) return 0;
    take = g_nodes[n].size - offset;
    if (take > out_len) take = out_len;
    take = vfs_read_range(&g_nodes[n], offset, out, take);
    cache_put_node_path(path, &g_nodes[n]);
    return (int)take;
}

int vfs_delete(const char* path) {
    int n = resolve_path(path);
    if (n < 0) return -1;
    if (g_nodes[n].parent_id == 0u) return -1;
    if (g_nodes[n].locked || vfs_is_critical_path(path)) return -1;

    int p = -1;
    for (u32 i = 0; i < VFS_MAX_NODES; i++) {
        if (g_nodes[i].used && g_nodes[i].id == g_nodes[n].parent_id) {
            p = (int)i;
            break;
        }
    }
    if (p < 0 || !has_access(&g_nodes[p], VFS_ACC_WRITE | VFS_ACC_EXEC)) return -1;

    if (g_nodes[n].type == VFS_NODE_DIR) {
        for (u32 i = 0; i < VFS_MAX_NODES; i++) {
            if (g_nodes[i].used && g_nodes[i].parent_id == g_nodes[n].id) {
                return -1;
            }
        }
    }

    u32 id = g_nodes[n].id;
    u32 parent = g_nodes[n].parent_id;
    char nm[VFS_MAX_NAME];
    set_name(nm, g_nodes[n].name);
    g_nodes[n].used = 0u;
    g_nodes[n].type = VFS_NODE_NONE;
    g_nodes[n].id = 0u;
    g_nodes[n].parent_id = 0u;
    g_nodes[n].size = 0u;
    vfs_clear_node_blocks(&g_nodes[n]);
    zero_name(g_nodes[n].name);
    cache_invalidate_path(path);
    {
        char parent_path[96];
        path_parent_of(path, parent_path, sizeof(parent_path));
        cache_invalidate_path(parent_path);
    }
    journal_push(VFS_J_DELETE, 0, parent, id, 0u, nm);
    if (g_backend.node_deleted) {
        (void)g_backend.node_deleted(id, parent);
    }
    vfs_flush_if_ready();
    return 0;
}

int vfs_rename(const char* path, const char* new_name) {
    if (!is_valid_name(new_name)) return -1;
    int n = resolve_path(path);
    if (n < 0 || g_nodes[n].parent_id == 0u || g_nodes[n].locked ||
        vfs_is_critical_path(path)) return -1;
    if (find_child(g_nodes[n].parent_id, new_name) >= 0) return -1;
    if (!has_access(&g_nodes[n], VFS_ACC_WRITE)) return -1;
    cache_invalidate_path(path);
    set_name(g_nodes[n].name, new_name);
    g_nodes[n].modified_tick = timer_ticks();
    {
        char parent_path[96];
        char new_path[128];
        path_parent_of(path, parent_path, sizeof(parent_path));
        new_path[0] = '\0';
        str_ncpy(new_path, parent_path, sizeof(new_path) - 1u);
        new_path[sizeof(new_path) - 1u] = '\0';
        if (str_cmp(new_path, "/") != 0) str_cat(new_path, "/");
        str_cat(new_path, new_name);
        cache_put_node_path(new_path, &g_nodes[n]);
        cache_invalidate_path(parent_path);
    }
    journal_push(VFS_J_RENAME, &g_nodes[n], g_nodes[n].parent_id, 0u, 0u, new_name);
    vfs_flush_if_ready();
    return 0;
}

int vfs_set_permissions(const char* path, u16 mode) {
    int n = resolve_path(path);
    if (n < 0) return -1;
    if (g_actor_uid != 0u && g_actor_uid != g_nodes[n].owner_uid) return -1;
    g_nodes[n].mode = (u16)(mode & 0777u);
    g_nodes[n].modified_tick = timer_ticks();
    cache_put_node_path(path, &g_nodes[n]);
    journal_push(VFS_J_CHMOD, &g_nodes[n], g_nodes[n].parent_id, g_nodes[n].mode, 0u, g_nodes[n].name);
    vfs_flush_if_ready();
    return 0;
}

int vfs_set_owner(const char* path, u32 uid, u32 gid) {
    int n = resolve_path(path);
    if (n < 0) return -1;
    if (g_actor_uid != 0u) return -1;
    g_nodes[n].owner_uid = uid;
    g_nodes[n].owner_gid = gid;
    g_nodes[n].modified_tick = timer_ticks();
    cache_put_node_path(path, &g_nodes[n]);
    journal_push(VFS_J_CHOWN, &g_nodes[n], g_nodes[n].parent_id, uid, gid, g_nodes[n].name);
    vfs_flush_if_ready();
    return 0;
}

int vfs_set_lock(const char* path, u8 locked) {
    int n = resolve_path(path);
    if (n < 0) return -1;
    if (g_actor_uid != 0u && g_actor_uid != g_nodes[n].owner_uid) return -1;
    if (vfs_is_critical_path(path) && !locked) return -1;
    g_nodes[n].locked = locked ? 1u : 0u;
    g_nodes[n].modified_tick = timer_ticks();
    cache_put_node_path(path, &g_nodes[n]);
    journal_push(VFS_J_LOCK, &g_nodes[n], g_nodes[n].parent_id, g_nodes[n].locked, 0u, g_nodes[n].name);
    vfs_flush_if_ready();
    return 0;
}

int vfs_access(const char* path, u8 access_mask) {
    int n = resolve_path(path);
    if (n < 0) return -1;
    return has_access(&g_nodes[n], access_mask) ? 0 : -1;
}

int vfs_info(const char* path, vfs_node_info_t* out) {
    if (!out) return -1;
    int n = resolve_path(path);
    if (n < 0) return -1;
    out->used = g_nodes[n].used;
    out->locked = g_nodes[n].locked;
    out->type = g_nodes[n].type;
    out->id = g_nodes[n].id;
    out->parent_id = g_nodes[n].parent_id;
    set_name(out->name, g_nodes[n].name);
    out->size = g_nodes[n].size;
    out->created_tick = g_nodes[n].created_tick;
    out->modified_tick = g_nodes[n].modified_tick;
    out->owner_uid = g_nodes[n].owner_uid;
    out->owner_gid = g_nodes[n].owner_gid;
    out->mode = g_nodes[n].mode;
    cache_put_node_path(path, &g_nodes[n]);
    return 0;
}

u32 vfs_list(const char* path, vfs_node_info_t* out, u32 max_out) {
    int n = resolve_path(path);
    if (n < 0 || g_nodes[n].type != VFS_NODE_DIR || !out || max_out == 0u) return 0u;
    if (!has_access(&g_nodes[n], VFS_ACC_READ | VFS_ACC_EXEC)) return 0u;
    u32 c = 0u;
    for (u32 i = 0; i < VFS_MAX_NODES && c < max_out; i++) {
        if (!g_nodes[i].used || g_nodes[i].parent_id != g_nodes[n].id) continue;
        out[c].used = g_nodes[i].used;
        out[c].locked = g_nodes[i].locked;
        out[c].type = g_nodes[i].type;
        out[c].id = g_nodes[i].id;
        out[c].parent_id = g_nodes[i].parent_id;
        set_name(out[c].name, g_nodes[i].name);
        out[c].size = g_nodes[i].size;
        out[c].created_tick = g_nodes[i].created_tick;
        out[c].modified_tick = g_nodes[i].modified_tick;
        out[c].owner_uid = g_nodes[i].owner_uid;
        out[c].owner_gid = g_nodes[i].owner_gid;
        out[c].mode = g_nodes[i].mode;
        c++;
    }
    return c;
}

void vfs_stats(vfs_stats_t* out) {
    if (!out) return;
    out->total_nodes = VFS_MAX_NODES;
    out->active_nodes = 0u;
    out->dirs = 0u;
    out->files = 0u;
    for (u32 i = 0; i < VFS_MAX_NODES; i++) {
        if (!g_nodes[i].used) continue;
        out->active_nodes++;
        if (g_nodes[i].type == VFS_NODE_DIR) out->dirs++;
        if (g_nodes[i].type == VFS_NODE_FILE) out->files++;
    }
}

u32 vfs_journal_count(void) {
    return g_journal_head;
}

int vfs_journal_get(u32 reverse_index, vfs_journal_entry_t* out) {
    if (!out) return -1;
    if (g_journal_head == 0u || reverse_index >= VFS_JOURNAL_CAP || reverse_index >= g_journal_head) {
        return -1;
    }
    u32 pos = (g_journal_head - 1u - reverse_index) % VFS_JOURNAL_CAP;
    if (g_journal[pos].id == 0u) return -1;
    *out = g_journal[pos];
    return 0;
}
