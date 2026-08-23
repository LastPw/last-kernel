#include "linux_rootfs.h"
#include "string.h"
#include "serial.h"

/*
 * LastOS Virtual Linux RootFS & Shared Library Subsystem
 *
 * Provides Linux dynamic linkers and shared libraries (/lib64/ld-linux-x86-64.so.2,
 * libc.so.6, libpthread.so.0, libm.so.6, libwayland-client.so.0) and essential
 * configuration files (/etc/resolv.conf, /etc/hosts, /etc/passwd) inside the sandbox.
 */

static rootfs_entry_t s_rootfs_table[ROOTFS_MAX_ENTRIES];
static u8 s_rootfs_inited = 0;

/* Virtual /etc configuration contents */
static const char* s_resolv_conf = "nameserver 1.1.1.1\nnameserver 8.8.8.8\n";
static const char* s_hosts_conf = "127.0.0.1\tlocalhost lastos-node\n::1\t\tlocalhost ip6-localhost\n";
static const char* s_passwd_conf = "root:x:0:0:root:/root:/bin/sh\nlastos:x:1000:1000:lastos:/home/lastos:/bin/sh\n";
static const char* s_group_conf = "root:x:0:\nlastos:x:1000:\nwheel:x:10:lastos\n";

/* Standard ELF Header Stub for virtual shared libraries */
static const u8 s_elf_so_stub[64] = {
    0x7F, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* ELF64 SysV */
    3, 0, /* e_type = ET_DYN */
    0x3E, 0, /* e_machine = x86_64 */
    1, 0, 0, 0, /* e_version = 1 */
    0x00, 0x10, 0, 0, 0, 0, 0, 0 /* e_entry */
};

int linux_rootfs_init(void) {
    if (s_rootfs_inited) return 0;

    for (u32 i = 0; i < ROOTFS_MAX_ENTRIES; i++) {
        s_rootfs_table[i].active = 0;
    }

    /* Register virtual /etc files */
    linux_rootfs_register_file("/etc/resolv.conf", (const u8*)s_resolv_conf, (u32)str_len(s_resolv_conf), ROOTFS_FILE_ETC_CONFIG);
    linux_rootfs_register_file("/etc/hosts", (const u8*)s_hosts_conf, (u32)str_len(s_hosts_conf), ROOTFS_FILE_ETC_CONFIG);
    linux_rootfs_register_file("/etc/passwd", (const u8*)s_passwd_conf, (u32)str_len(s_passwd_conf), ROOTFS_FILE_ETC_CONFIG);
    linux_rootfs_register_file("/etc/group", (const u8*)s_group_conf, (u32)str_len(s_group_conf), ROOTFS_FILE_ETC_CONFIG);

    /* Register standard Linux shared library locations */
    linux_rootfs_register_file("/lib64/ld-linux-x86-64.so.2", s_elf_so_stub, sizeof(s_elf_so_stub), ROOTFS_FILE_DYNAMIC_LIBC);
    linux_rootfs_register_file("/lib64/libc.so.6", s_elf_so_stub, sizeof(s_elf_so_stub), ROOTFS_FILE_DYNAMIC_LIBC);
    linux_rootfs_register_file("/lib64/libm.so.6", s_elf_so_stub, sizeof(s_elf_so_stub), ROOTFS_FILE_DYNAMIC_LIBM);
    linux_rootfs_register_file("/lib64/libpthread.so.0", s_elf_so_stub, sizeof(s_elf_so_stub), ROOTFS_FILE_DYNAMIC_PTHREAD);
    linux_rootfs_register_file("/lib64/libdl.so.2", s_elf_so_stub, sizeof(s_elf_so_stub), ROOTFS_FILE_DYNAMIC_LIBC);
    linux_rootfs_register_file("/lib64/librt.so.1", s_elf_so_stub, sizeof(s_elf_so_stub), ROOTFS_FILE_DYNAMIC_LIBC);
    linux_rootfs_register_file("/usr/lib64/libwayland-client.so.0", s_elf_so_stub, sizeof(s_elf_so_stub), ROOTFS_FILE_DYNAMIC_WAYLAND);

    s_rootfs_inited = 1;
    serial_write("[LINUX_ROOTFS] Initialized Linux Virtual RootFS & Libs Tree\n");
    return 0;
}

int linux_rootfs_register_file(const char* vpath, const u8* data, u32 size, rootfs_type_t type) {
    if (!vpath) return -1;

    for (u32 i = 0; i < ROOTFS_MAX_ENTRIES; i++) {
        if (!s_rootfs_table[i].active) {
            rootfs_entry_t* e = &s_rootfs_table[i];
            e->active = 1;
            str_ncpy(e->virtual_path, vpath, sizeof(e->virtual_path) - 1);
            e->content_ptr = data;
            e->content_size = size;
            e->type = type;
            e->is_directory = 0;
            return (int)i;
        }
    }
    return -1;
}

const rootfs_entry_t* linux_rootfs_lookup(const char* vpath) {
    if (!s_rootfs_inited) linux_rootfs_init();
    if (!vpath) return 0;

    for (u32 i = 0; i < ROOTFS_MAX_ENTRIES; i++) {
        if (s_rootfs_table[i].active) {
            if (str_cmp(s_rootfs_table[i].virtual_path, vpath) == 0 ||
                str_str(vpath, s_rootfs_table[i].virtual_path) != 0) {
                return &s_rootfs_table[i];
            }
        }
    }
    return 0;
}

int linux_rootfs_read(const char* vpath, u8* out_buf, u32 max_len, u32 offset) {
    const rootfs_entry_t* e = linux_rootfs_lookup(vpath);
    if (!e || !out_buf || !e->content_ptr) return -1;

    if (offset >= e->content_size) return 0;

    u32 avail = e->content_size - offset;
    u32 copy_len = (max_len < avail) ? max_len : avail;

    for (u32 i = 0; i < copy_len; i++) {
        out_buf[i] = e->content_ptr[offset + i];
    }
    return (int)copy_len;
}
