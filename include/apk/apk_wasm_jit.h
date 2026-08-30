#ifndef APK_WASM_JIT_H
#define APK_WASM_JIT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JIT_CODE_BUFFER_SIZE 4096

typedef struct {
    u8  code_buffer[JIT_CODE_BUFFER_SIZE];
    u32 code_size;
    u32 compiled_functions;
    u8  is_ready;
} apk_wasm_jit_t;

int apk_wasm_jit_init(void);
int apk_wasm_jit_compile(const u8* wasm_code, u32 code_size, u8* out_x86_code, u32* out_x86_size);
int apk_wasm_jit_exec_simple(const u8* wasm_code, u32 code_size, u32* out_result);

#ifdef __cplusplus
}
#endif

#endif /* APK_WASM_JIT_H */
