#ifndef APK_PE_H
#define APK_PE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PE_MAGIC_DOS      0x5A4Du /* 'MZ' */
#define PE_MAGIC_NT       0x00004550u /* 'PE\0\0' */
#define PE_OPT_MAGIC_PE32P 0x020Bu /* PE32+ (64-bit) */

#define PE_MAX_SECTIONS   16

typedef struct {
    char name[8];
    u32  virtual_size;
    u32  virtual_address;
    u32  size_of_raw_data;
    u32  pointer_to_raw_data;
    u32  characteristics;
} pe_section_header_t;

typedef struct {
    u16 machine;
    u16 number_of_sections;
    u32 time_date_stamp;
    u32 pointer_to_symbol_table;
    u32 number_of_symbols;
    u16 size_of_optional_header;
    u16 characteristics;
} pe_file_header_t;

typedef struct {
    u16 magic;
    u8  major_linker_version;
    u8  minor_linker_version;
    u32 size_of_code;
    u32 size_of_initialized_data;
    u32 size_of_uninitialized_data;
    u32 address_of_entry_point;
    u32 base_of_code;
    u64 image_base;
    u32 section_alignment;
    u32 file_alignment;
    u16 major_os_version;
    u16 minor_os_version;
    u16 major_image_version;
    u16 minor_image_version;
    u16 major_subsystem_version;
    u16 minor_subsystem_version;
    u32 win32_version_value;
    u32 size_of_image;
    u32 size_of_headers;
    u32 check_sum;
    u16 subsystem;
    u16 dll_characteristics;
    u64 size_of_stack_reserve;
    u64 size_of_stack_commit;
    u64 size_of_heap_reserve;
    u64 size_of_heap_commit;
    u32 loader_flags;
    u32 number_of_rva_and_sizes;
} pe_optional_header64_t;

typedef struct {
    u8  is_valid;
    u64 entry_point_vaddr;
    u64 image_base;
    u32 image_size;
    u16 section_count;
    pe_section_header_t sections[PE_MAX_SECTIONS];
} pe_binary_info_t;

int pe_parse_header(const u8* data, u32 size, pe_binary_info_t* out_info);
int pe_load_and_run(const u8* data, u32 size, int argc, const char** argv);

#ifdef __cplusplus
}
#endif

#endif /* APK_PE_H */
