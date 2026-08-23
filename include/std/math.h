#ifndef LASTOS_FS_MATH_H
#define LASTOS_FS_MATH_H

#include "stdint.h"
#include "stddef.h"

#define HUGE_VAL  (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define INFINITY  (__builtin_inff())
#define NAN       (__builtin_nanf(""))

#define isnan(x)      __builtin_isnan(x)
#define isinf(x)      __builtin_isinf(x)
#define isfinite(x)   __builtin_isfinite(x)
#define signbit(x)    __builtin_signbit(x)

#define M_PI          3.14159265358979323846
#define M_E           2.71828182845904523536
#define M_LN2         0.69314718055994530942
#define M_LN10        2.30258509299404568402
#define M_LOG2E       1.44269504088896340736
#define M_LOG10E      0.43429448190325182765
#define M_SQRT2       1.41421356237309504880
#define M_SQRT1_2     0.70710678118654752440

static inline double fabs(double x) { return __builtin_fabs(x); }
static inline float  fabsf(float x) { return __builtin_fabsf(x); }
static inline double floor(double x) { return __builtin_floor(x); }
static inline float  floorf(float x) { return __builtin_floorf(x); }
static inline double ceil(double x) { return __builtin_ceil(x); }
static inline float  ceilf(float x) { return __builtin_ceilf(x); }
static inline double trunc(double x) { return __builtin_trunc(x); }
static inline double round(double x) { return __builtin_round(x); }
static inline long   lrint(double x) { return __builtin_lrint(x); }
static inline long long llrint(double x) { return __builtin_llrint(x); }
static inline double fmod(double x, double y) { return __builtin_fmod(x, y); }
static inline double sqrt(double x) { return __builtin_sqrt(x); }
static inline double hypot(double x, double y) { return __builtin_hypot(x, y); }
static inline float  hypotf(float x, float y) { return __builtin_hypotf(x, y); }
static inline double pow(double x, double y) { return __builtin_pow(x, y); }
static inline double exp(double x) { return __builtin_exp(x); }
static inline double log(double x) { return __builtin_log(x); }
static inline double log2(double x) { return __builtin_log2(x); }
static inline double log10(double x) { return __builtin_log10(x); }
static inline double sin(double x) { return __builtin_sin(x); }
static inline double cos(double x) { return __builtin_cos(x); }
static inline double tan(double x) { return __builtin_tan(x); }
static inline double asin(double x) { return __builtin_asin(x); }
static inline double acos(double x) { return __builtin_acos(x); }
static inline double atan(double x) { return __builtin_atan(x); }
static inline double atan2(double y, double x) { return __builtin_atan2(y, x); }
static inline double sinh(double x) { return __builtin_sinh(x); }
static inline double cosh(double x) { return __builtin_cosh(x); }
static inline double tanh(double x) { return __builtin_tanh(x); }
static inline double asinh(double x) { return __builtin_asinh(x); }
static inline double acosh(double x) { return __builtin_acosh(x); }
static inline double atanh(double x) { return __builtin_atanh(x); }
static inline double expm1(double x) { return __builtin_expm1(x); }
static inline double log1p(double x) { return __builtin_log1p(x); }
static inline double cbrt(double x) { return __builtin_cbrt(x); }
static inline double copysign(double x, double y) { return __builtin_copysign(x, y); }
static inline double fmin(double x, double y) { return __builtin_fmin(x, y); }
static inline double fmax(double x, double y) { return __builtin_fmax(x, y); }
static inline double modf(double x, double *iptr) { return __builtin_modf(x, iptr); }

#endif /* LASTOS_FS_MATH_H */
