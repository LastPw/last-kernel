#ifndef APK_WASM_H
#define APK_WASM_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WASM_MAGIC 0x6D736100u /* '\0asm' in Little Endian */
#define WASM_VERSION 0x01u

#define WASM_SEC_TYPE     1
#define WASM_SEC_IMPORT   2
#define WASM_SEC_FUNCTION 3
#define WASM_SEC_TABLE    4
#define WASM_SEC_MEMORY   5
#define WASM_SEC_GLOBAL   6
#define WASM_SEC_EXPORT   7
#define WASM_SEC_START    8
#define WASM_SEC_ELEMENT  9
#define WASM_SEC_CODE     10
#define WASM_SEC_DATA     11

typedef struct {
    u8  is_valid;
    u32 version;
    u32 function_count;
    u32 export_count;
    u32 memory_pages;
    u32 code_section_offset;
    u32 code_section_size;
} wasm_module_t;

int wasm_parse_module(const u8* data, u32 size, wasm_module_t* out_mod);
int wasm_execute_module(const u8* data, u32 size);

#ifdef __cplusplus
}
#endif

#endif /* APK_WASM_H */
