#ifndef LASTOS_FS_FENV_H
#define LASTOS_FS_FENV_H

#define FE_TONEAREST  0
#define FE_UPWARD     1
#define FE_DOWNWARD   2
#define FE_TOWARDZERO 3

#define FE_INEXACT    0x01
#define FE_UNDERFLOW  0x02
#define FE_OVERFLOW   0x04
#define FE_DIVBYZERO  0x08
#define FE_INVALID    0x10
#define FE_ALL_EXCEPT 0x1f

typedef unsigned short fexcept_t;
typedef struct {
    unsigned short __control_word;
    unsigned short __glibc_reserved1;
    unsigned short __status_word;
    unsigned short __glibc_reserved2;
    unsigned short __tags;
    unsigned short __glibc_reserved3;
    unsigned int   __eip;
    unsigned short __cs_selector;
    unsigned short __opcode;
    unsigned int   __data_offset;
    unsigned short __data_selector;
    unsigned short __glibc_reserved4;
    unsigned int   __mxcsr;
} fenv_t;

#define FE_DFL_ENV ((const fenv_t *) -1)

static inline int feclearexcept(int excepts) { (void)excepts; return 0; }
static inline int feraiseexcept(int excepts) { (void)excepts; return 0; }
static inline int fetestexcept(int excepts) { (void)excepts; return 0; }
static inline int fegetround(void) { return FE_TONEAREST; }
static inline int fesetround(int rounding_mode) { (void)rounding_mode; return 0; }
static inline int fegetenv(fenv_t *envp) { (void)envp; return 0; }
static inline int fesetenv(const fenv_t *envp) { (void)envp; return 0; }
static inline int feupdateenv(const fenv_t *envp) { (void)envp; return 0; }

#endif /* LASTOS_FS_FENV_H */
