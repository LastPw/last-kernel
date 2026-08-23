#ifndef LASTOS_UNIVERSAL_RUNTIME_H
#define LASTOS_UNIVERSAL_RUNTIME_H

#include "types.h"

#define BIN_FORMAT_UNKNOWN 0u
#define BIN_FORMAT_ELF64   1u
#define BIN_FORMAT_PE32P   2u
#define BIN_FORMAT_WASM    3u

/* DOS / PE Headers */
#define DOS_MAGIC 0x5A4Du /* "MZ" */
#define PE_MAGIC  0x00004550u /* "PE\0\0" */
#define PE_OPT_HDR64_MAGIC 0x020Bu

typedef struct __attribute__((packed)) {
    u16 e_magic;    /* "MZ" */
    u16 e_cblp;
    u16 e_cp;
    u16 e_crlc;
    u16 e_cparhdr;
    u16 e_minalloc;
    u16 e_maxalloc;
    u16 e_ss;
    u16 e_sp;
    u16 e_csum;
    u16 e_ip;
    u16 e_cs;
    u16 e_lfarlc;
    u16 e_ovno;
    u16 e_res[4];
    u16 e_oemid;
    u16 e_oeminfo;
    u16 e_res2[10];
    u32 e_lfanew;   /* Offset to PE Header */
} dos_header_t;

typedef struct __attribute__((packed)) {
    u32 signature;          /* "PE\0\0" */
    u16 machine;            /* 0x8664 for x86_64 */
    u16 number_of_sections;
    u32 time_date_stamp;
    u32 pointer_to_symbol_table;
    u32 number_of_symbols;
    u16 size_of_optional_header;
    u16 characteristics;
} pe_file_header_t;

typedef struct {
    u32 total_binaries_executed;
    u32 elf64_executions;
    u32 pe32p_executions;
    u32 wasm_executions;
    u32 direct_syscall_transpositions;
    u32 avg_transposition_cycles;
    u8  pe_runtime_active;
    u8  elf_runtime_active;
    u8  wasm_runtime_active;
} universal_runtime_stats_t;

void universal_runtime_init(void);
u32  universal_runtime_detect_format(const u8* binary_data, u32 len);
int  universal_runtime_execute(const char* path, const u8* data, u32 len);
void universal_runtime_get_stats(universal_runtime_stats_t* out);

#endif /* LASTOS_UNIVERSAL_RUNTIME_H */
