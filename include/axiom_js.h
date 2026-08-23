#ifndef LASTOS_AXIOM_JS_H
#define LASTOS_AXIOM_JS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JS_MAX_VARS       128
#define JS_MAX_STR_LEN    256
#define JS_MAX_FUNCS      32
#define JS_MAX_CALL_DEPTH 16
#define JS_MAX_INSTRUCTIONS 50000

typedef enum {
    JS_T_UNDEFINED = 0,
    JS_T_NULL,
    JS_T_BOOL,
    JS_T_NUM,
    JS_T_STR,
    JS_T_DOM_NODE,
    JS_T_FUNC
} js_type_t;

struct dom_node;

typedef struct js_val {
    js_type_t type;
    union {
        u8               b;
        i32              num;
        char             str[JS_MAX_STR_LEN];
        struct dom_node* node;
        u32              func_id;
    } as;
} js_val_t;

typedef struct {
    char     name[32];
    js_val_t val;
    u8       is_const;
} js_var_t;

typedef struct {
    char name[32];
    char param_name[32];
    char body[512];
} js_func_t;

typedef struct {
    js_var_t  vars[JS_MAX_VARS];
    u32       var_count;
    js_func_t funcs[JS_MAX_FUNCS];
    u32       func_count;
    
    /* Sandbox security state */
    u32       instructions_executed;
    u32       max_instructions;
    u8        sandbox_killed;
    u8        sandbox_error;
    char      error_msg[96];
} js_vm_t;

/* VM Lifecycle */
void     js_vm_init(js_vm_t* vm);
void     js_vm_reset(js_vm_t* vm);
js_val_t js_eval(js_vm_t* vm, const char* code);
js_val_t js_eval_expression(js_vm_t* vm, const char* expr, const char** out_rest);

/* Value Helpers */
js_val_t js_make_undefined(void);
js_val_t js_make_null(void);
js_val_t js_make_bool(u8 b);
js_val_t js_make_num(i32 num);
js_val_t js_make_str(const char* str);
js_val_t js_make_node(struct dom_node* node);
void     js_val_to_string(const js_val_t* val, char* buf, u32 max_len);

/* Variable & Function Environment */
void      js_set_var(js_vm_t* vm, const char* name, js_val_t val);
js_val_t* js_get_var(js_vm_t* vm, const char* name);

/* Sandbox Controls */
void     js_sandbox_set_limit(js_vm_t* vm, u32 max_instructions);
u8       js_sandbox_is_alive(js_vm_t* vm);

#ifdef __cplusplus
}
#endif

#endif /* LASTOS_AXIOM_JS_H */
