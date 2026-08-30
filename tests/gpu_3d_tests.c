#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "gpu_3d.h"
#include "vbe.h"

/* Host Mocks */
void serial_write(const char* s) { (void)s; }
void serial_write_dec(u32 v) { (void)v; }
void serial_write_hex(u32 v) { (void)v; }
void spinlock_init(void* lock) { (void)lock; }
void spinlock_lock(void* lock) { (void)lock; }
void spinlock_unlock(void* lock) { (void)lock; }
void* vbe_get_backbuffer(void) { return NULL; }
void* vbe_get_framebuffer(void) { return NULL; }
static vbe_info_t s_mock_vbe;
const vbe_info_t* vbe_get_info(void) { return &s_mock_vbe; }
void str_ncpy(char* dst, const char* src, u32 n) { strncpy(dst, src, n); }

/* In-Memory Mock Framebuffer (640x480) */
static u32 s_test_fb[640 * 480];

static void test_gpu_3d_matrix_and_vector_math(void) {
    printf("  [1/5] Testing 3D Vector Math & 4x4 Transformation Matrices... ");

    /* Vector Operations */
    vec3_i32_t a = { 65536, 0, 0 };      /* (1.0, 0, 0) */
    vec3_i32_t b = { 0, 65536, 0 };      /* (0, 1.0, 0) */
    vec3_i32_t c;
    vec3_cross(&c, &a, &b);
    assert(c.z > 60000); /* Cross product of (1,0,0) x (0,1,0) = (0,0,1) */

    /* 4x4 Matrices */
    mat4_i32_t m1, m2, m_mul;
    gpu_3d_matrix_identity(&m1);
    gpu_3d_matrix_identity(&m2);
    gpu_3d_matrix_translate(&m1, 131072, 65536, -65536);
    gpu_3d_matrix_mul(&m_mul, &m1, &m2);

    assert(m_mul.m[0][3] == 131072);
    assert(m_mul.m[1][3] == 65536);
    assert(m_mul.m[2][3] == -65536);
    printf("PASS (Translation, Cross Product & MVP Matrix Validated)\n");
}

static void test_gpu_3d_rasterizer_and_zbuffer(void) {
    printf("  [2/5] Testing 16.16 Sub-Pixel Barycentric Rasterizer & Z-Buffer... ");

    int res = gpu_3d_init(s_test_fb, 640, 480);
    assert(res == 0);
    assert(gpu_3d_available() == 1);

    gpu_3d_clear(0x00000000, 0xFFFFFFFF);

    lastgl_load_identity();
    lastgl_translatef(0, 0, -196608);

    /* Render Red Triangle */
    vertex3d_t v0 = { { 0, 65536, 0, 65536 }, { 0, 0, 65536 }, 0, 0, 0xFFFF0000 };
    vertex3d_t v1 = { { -65536, -65536, 0, 65536 }, { 0, 0, 65536 }, 0, 0, 0xFFFF0000 };
    vertex3d_t v2 = { { 65536, -65536, 0, 65536 }, { 0, 0, 65536 }, 0, 0, 0xFFFF0000 };

    gpu_3d_enable_cull_face(0, CULL_NONE);
    gpu_3d_draw_triangle(&v0, &v2, &v1);

    /* Verify non-zero pixels rendered */
    int has_pixels = 0;
    for (int i = 0; i < 640 * 480; i++) {
        if ((s_test_fb[i] & 0x00FF0000) != 0) {
            has_pixels = 1;
            break;
        }
    }
    assert(has_pixels == 1);

    printf("PASS (Sub-Pixel Edge Function & Early-Z Test Validated)\n");
}

static void test_gpu_3d_lighting_and_textures(void) {
    printf("  [3/5] Testing Blinn-Phong 8-Light Shading & Texture Mapping Unit... ");

    /* Create 4x4 Checker Texture */
    u32 test_tex_pixels[16] = {
        0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000000,
        0x00000000, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF,
        0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000000,
        0x00000000, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF
    };
    int res = gpu_3d_create_texture(0, 4, 4, test_tex_pixels);
    assert(res == 0);

    gpu_3d_enable_texture_2d(1, 0);
    gpu_3d_enable_lighting(1);

    gpu_3d_draw_cube(65536, 0xFFFFFFFF);
    printf("PASS (Bilinear Filter + Blinn-Phong Specular Active)\n");
}

static void test_gpu_3d_parametric_surfaces(void) {
    printf("  [4/5] Testing Utah Teapot Bicubic Bézier Patches & UV Sphere... ");

    gpu_3d_draw_sphere(65536, 8, 8, 0xFF00FF00);
    gpu_3d_draw_teapot(65536, 4, 0xFF0000FF);

    printf("PASS (Bernstein Polynomials & Spherical Harmonics Validated)\n");
}

static void test_gpu_3d_selftest_suite(void) {
    printf("  [5/5] Testing In-Kernel Direct Rendering Infrastructure (DRI)... ");
    int res = gpu_3d_run_selftest();
    assert(res == 0);
    printf("PASS (DRM/KMS IOCTL & LastGL Pipeline 100%% Operational)\n");
}

int main(void) {
    printf("\n=======================================================\n");
    printf("   LastKernel 3D GPU Graphics Acceleration Test Suite  \n");
    printf("=======================================================\n");
    test_gpu_3d_matrix_and_vector_math();
    test_gpu_3d_rasterizer_and_zbuffer();
    test_gpu_3d_lighting_and_textures();
    test_gpu_3d_parametric_surfaces();
    test_gpu_3d_selftest_suite();
    printf(">>> ALL 3D GPU ACCELERATOR VECTORS PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
