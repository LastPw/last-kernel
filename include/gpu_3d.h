#ifndef DRIVERS_VIDEO_GPU_3D_H
#define DRIVERS_VIDEO_GPU_3D_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 3D Vector & Matrix Types (Q16 Fixed-Point & Float Unions) ──────────── */
typedef struct {
    i32 x, y, z;
} vec3_i32_t;

typedef struct {
    i32 x, y, z, w;
} vec4_i32_t;

typedef struct {
    i32 m[4][4]; /* 4x4 Transformation Matrix in Q16 Fixed-Point */
} mat4_i32_t;

/* ─── 3D Vertex Definition ───────────────────────────────────────────────── */
typedef struct {
    vec4_i32_t pos;      /* Object / World / Clip Space (x, y, z, w) */
    vec3_i32_t normal;   /* Surface Normal (nx, ny, nz) in Q14 */
    i32        u, v;     /* UV Texture Coordinates in Q16 (0..65536 = 0.0..1.0) */
    u32        color;    /* ARGB 32-Bit Vertex Color */
} vertex3d_t;

/* ─── 3D Triangle Representation ─────────────────────────────────────────── */
typedef struct {
    vertex3d_t v[3];
} triangle3d_t;

/* ─── Light Source Descriptor ────────────────────────────────────────────── */
typedef enum {
    LIGHT_TYPE_DIRECTIONAL = 0, /* Infinite Sun Light (Direction only) */
    LIGHT_TYPE_POINT       = 1, /* Omni-directional Point Light with Attenuation */
    LIGHT_TYPE_SPOT        = 2, /* Conical Spotlight */
} light_type_t;

typedef struct {
    u8           is_enabled;
    light_type_t type;
    vec3_i32_t   position;     /* World coordinates */
    vec3_i32_t   direction;    /* Direction vector (normalized Q14) */
    u32          ambient_col;  /* ARGB */
    u32          diffuse_col;  /* ARGB */
    u32          specular_col; /* ARGB */
    u32          atten_const;  /* Attenuation c0 */
    u32          atten_linear; /* Attenuation c1 in Q16 */
    u32          atten_quad;   /* Attenuation c2 in Q16 */
    u16          spot_cutoff;  /* Cosine of cutoff angle in Q14 */
    u16          spot_exponent;
} light3d_t;

/* ─── Material Properties ────────────────────────────────────────────────── */
typedef struct {
    u32 ambient;
    u32 diffuse;
    u32 specular;
    u32 emissive;
    u16 shininess;     /* Specular Exponent (1..128) */
    u8  has_texture;
    u8  has_normal_map;
    u16 texture_id;
    u16 normal_map_id;
} material3d_t;

/* ─── Texture Descriptor & Wrap / Filter Modes ───────────────────────────── */
typedef enum {
    TEX_WRAP_REPEAT        = 0,
    TEX_WRAP_CLAMP_TO_EDGE = 1,
    TEX_WRAP_MIRROR        = 2,
} tex_wrap_mode_t;

typedef enum {
    TEX_FILTER_NEAREST  = 0,
    TEX_FILTER_BILINEAR = 1,
    TEX_FILTER_TRILINEAR= 2,
} tex_filter_mode_t;

typedef struct {
    u16               id;
    u16               width;
    u16               height;
    u8                levels_count; /* Mipmap levels (0..9) */
    tex_wrap_mode_t   wrap_s;
    tex_wrap_mode_t   wrap_t;
    tex_filter_mode_t min_filter;
    tex_filter_mode_t mag_filter;
    const u32*        pixels[10];   /* Pointer to level 0..9 mipmap data */
} texture2d_t;

/* ─── Shading & Depth Modes ──────────────────────────────────────────────── */
typedef enum {
    SHADE_FLAT    = 0,
    SHADE_GOURAUD = 1,
    SHADE_PHONG   = 2,
} shading_mode_t;

typedef enum {
    DEPTH_TEST_NEVER    = 0,
    DEPTH_TEST_LESS     = 1,
    DEPTH_TEST_EQUAL    = 2,
    DEPTH_TEST_LEQUAL   = 3,
    DEPTH_TEST_GREATER  = 4,
    DEPTH_TEST_NOTEQUAL = 5,
    DEPTH_TEST_GEQUAL   = 6,
    DEPTH_TEST_ALWAYS   = 7,
} depth_func_t;

typedef enum {
    CULL_NONE  = 0,
    CULL_FRONT = 1,
    CULL_BACK  = 2,
} cull_mode_t;

typedef enum {
    BLEND_NONE            = 0,
    BLEND_ALPHA           = 1, /* SrcAlpha, 1 - SrcAlpha */
    BLEND_ADDITIVE        = 2, /* One, One */
    BLEND_MULTIPLICATIVE  = 3, /* DstColor, Zero */
} blend_mode_t;

/* ─── GPU 3D Pipeline Context & State Machine ────────────────────────────── */
typedef struct {
    u8             is_initialized;
    u32            screen_w;
    u32            screen_h;
    u32*           color_buffer;    /* 32-Bit TrueColor ARGB Framebuffer */
    u32*           depth_buffer;    /* 32-Bit Fixed-Point Z-Buffer */
    u8             depth_test_on;
    u8             depth_write_on;
    depth_func_t   depth_func;
    u8             cull_face_on;
    cull_mode_t    cull_mode;
    shading_mode_t shading_mode;
    blend_mode_t   blend_mode;
    u8             lighting_on;
    u8             texture_2d_on;
    u16            active_texture_id;
    light3d_t      lights[8];
    material3d_t   current_material;
    mat4_i32_t     model_matrix;
    mat4_i32_t     view_matrix;
    mat4_i32_t     proj_matrix;
    mat4_i32_t     mvp_matrix;      /* Combined Model-View-Projection Matrix */
    mat4_i32_t     normal_matrix;   /* Inverse Transpose for Normals */
    u32            viewport_x;
    u32            viewport_y;
    u32            viewport_w;
    u32            viewport_h;
    u64            rendered_triangles_count;
    u64            culled_triangles_count;
    u64            rendered_pixels_count;
} gpu_3d_context_t;

/* ─── Public 3D Graphics Accelerator APIs ────────────────────────────────── */
int  gpu_3d_init(u32* framebuffer, u32 width, u32 height);
int  gpu_3d_available(void);
void gpu_3d_clear(u32 color, u32 depth);
void gpu_3d_set_viewport(u32 x, u32 y, u32 w, u32 h);

/* 3D Transform & Matrix Pipeline */
void gpu_3d_matrix_identity(mat4_i32_t* m);
void gpu_3d_matrix_mul(mat4_i32_t* dst, const mat4_i32_t* a, const mat4_i32_t* b);
void gpu_3d_matrix_translate(mat4_i32_t* m, i32 tx, i32 ty, i32 tz);
void gpu_3d_matrix_scale(mat4_i32_t* m, i32 sx, i32 sy, i32 sz);
void gpu_3d_matrix_rotate_x(mat4_i32_t* m, i32 angle_deg);
void gpu_3d_matrix_rotate_y(mat4_i32_t* m, i32 angle_deg);
void gpu_3d_matrix_rotate_z(mat4_i32_t* m, i32 angle_deg);
void gpu_3d_matrix_perspective(mat4_i32_t* m, i32 fov_deg, i32 aspect_q16, i32 z_near, i32 z_far);
void gpu_3d_matrix_ortho(mat4_i32_t* m, i32 left, i32 right, i32 bottom, i32 top, i32 z_near, i32 z_far);
void gpu_3d_matrix_lookat(mat4_i32_t* m, const vec3_i32_t* eye, const vec3_i32_t* target, const vec3_i32_t* up);
void gpu_3d_update_mvp(void);

/* Pipeline Render States */
void gpu_3d_enable_depth_test(u8 enable, depth_func_t func);
void gpu_3d_enable_cull_face(u8 enable, cull_mode_t mode);
void gpu_3d_enable_lighting(u8 enable);
void gpu_3d_enable_texture_2d(u8 enable, u16 texture_id);
void gpu_3d_set_blend_mode(blend_mode_t mode);
void gpu_3d_set_shading_mode(shading_mode_t mode);
void gpu_3d_set_light(u8 index, const light3d_t* light);
void gpu_3d_set_material(const material3d_t* material);

void vec3_cross(vec3_i32_t* out, const vec3_i32_t* a, const vec3_i32_t* b);

/* Geometry Drawing & Rasterization */
void gpu_3d_draw_triangle(const vertex3d_t* v0, const vertex3d_t* v1, const vertex3d_t* v2);
void gpu_3d_draw_mesh(const vertex3d_t* vertices, const u16* indices, u32 triangle_count);
void gpu_3d_draw_cube(i32 size, u32 color);
void gpu_3d_draw_sphere(i32 radius, u32 slices, u32 stacks, u32 color);
void gpu_3d_draw_teapot(i32 scale, u32 subdivisions, u32 color);

/* Texture Management */
int  gpu_3d_create_texture(u16 id, u16 w, u16 h, const u32* pixels);
void gpu_3d_bind_texture(u16 id);

/* LastGL OpenGL-Compatible Interface */
void lastgl_load_identity(void);
void lastgl_translatef(i32 x, i32 y, i32 z);
void lastgl_rotatef(i32 angle_deg, i32 ax, i32 ay, i32 az);
void lastgl_scalef(i32 sx, i32 sy, i32 sz);
void lastgl_color4ub(u8 r, u8 g, u8 b, u8 a);
void lastgl_vertex3f(i32 x, i32 y, i32 z);
void lastgl_begin_triangles(void);
void lastgl_end_triangles(void);

/* Status & Telemetry */
int  gpu_3d_run_selftest(void);
void gpu_3d_get_status(void);
void gpu_3d_dump_performance(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_VIDEO_GPU_3D_H */
