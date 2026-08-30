/* Minimal ELF definitions for loading kernel images (little-endian). */
#ifndef PULSE_ELF_H
#define PULSE_ELF_H

#include "pulsebase.h"

#define ELF_MAGIC0 0x7F
#define ELF_CLASS32 1
#define ELF_CLASS64 2
#define ELF_DATA2LSB 1
#define ELF_EV_CURRENT 1

#define ET_EXEC 2
#define EM_386 3
#define EM_X86_64 62
#define PT_LOAD 1

typedef struct {
    UINT8  e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} elf_ehdr64_t;                 /* sizeof == 64 */

typedef struct {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} elf_phdr64_t;                 /* sizeof == 56 */

typedef struct {
    UINT8  e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT32 e_entry;
    UINT32 e_phoff;
    UINT32 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} elf_ehdr32_t;                 /* sizeof == 52 */

typedef struct {
    UINT32 p_type;
    UINT32 p_offset;
    UINT32 p_vaddr;
    UINT32 p_paddr;
    UINT32 p_filesz;
    UINT32 p_memsz;
    UINT32 p_flags;
    UINT32 p_align;
} elf_phdr32_t;                 /* sizeof == 32 */

_Static_assert(sizeof(elf_ehdr64_t) == 64, "ELF64 ehdr size");
_Static_assert(sizeof(elf_phdr64_t) == 56, "ELF64 phdr size");
_Static_assert(sizeof(elf_ehdr32_t) == 52, "ELF32 ehdr size");
_Static_assert(sizeof(elf_phdr32_t) == 32, "ELF32 phdr size");

/* ---- load errors ---- */
enum {
    ELF_OK = 0,
    ELF_E_ARGS,
    ELF_E_MAGIC,
    ELF_E_CLASS,
    ELF_E_ENDIAN,
    ELF_E_ARCH,
    ELF_E_TRUNC,
    ELF_E_NOSEG,
    ELF_E_BADPH,
    ELF_E_RANGE,
    ELF_E_NOMEM,
};

/* Reserve [phys, phys+len) for a PT_LOAD segment; return a writable
   virtual address or NULL. The loader does its own copying/zeroing. */
typedef VOID *(*elf_reserve_fn)(VOID *ctx, UINT64 phys, UINT64 len);

typedef struct {
    elf_reserve_fn reserve;
} elf_callbacks_t;

/* Load an ELF image (ET_EXEC/ET_DYN, class 32 or 64, little-endian).
   On success returns ELF_OK and sets *out_entry (required) and
   *out_top (optional, may be NULL): one past the highest loaded byte. */
INT32 elf_load(const UINT8 *img, UINTN len, const elf_callbacks_t *cb,
               VOID *ctx, UINT64 *out_entry, UINT64 *out_top);

const char *elf_strerror(INT32 err);

#endif
