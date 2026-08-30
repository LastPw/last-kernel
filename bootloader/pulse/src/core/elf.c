#include "elf.h"
#include "lib.h"

static INT32 load_class(const UINT8 *img, UINTN len, int is64,
                        const elf_callbacks_t *cb, VOID *ctx,
                        UINT64 *out_entry, UINT64 *out_top)
{
    UINT64 entry, phoff;
    UINT16 phentsize, phnum, machine;
    UINTN  phdr_size, ehsize;

    if (is64) {
        elf_ehdr64_t eh;
        if (len < sizeof(eh))
            return ELF_E_TRUNC;
        memcpy(&eh, img, sizeof(eh));
        entry = eh.e_entry;
        phoff = eh.e_phoff;
        phentsize = eh.e_phentsize;
        phnum = eh.e_phnum;
        machine = eh.e_machine;
        phdr_size = sizeof(elf_phdr64_t);
        ehsize = sizeof(elf_ehdr64_t);
    } else {
        elf_ehdr32_t eh;
        if (len < sizeof(eh))
            return ELF_E_TRUNC;
        memcpy(&eh, img, sizeof(eh));
        entry = eh.e_entry;
        phoff = eh.e_phoff;
        phentsize = eh.e_phentsize;
        phnum = eh.e_phnum;
        machine = eh.e_machine;
        phdr_size = sizeof(elf_phdr32_t);
        ehsize = sizeof(elf_ehdr32_t);
    }

    if (img[6] != ELF_EV_CURRENT)   /* EI_VERSION */
        return ELF_E_MAGIC;
    if (machine != (is64 ? EM_X86_64 : EM_386))
        return ELF_E_ARCH;
    if (phnum == 0 || phoff < ehsize)
        return ELF_E_NOSEG;
    if (phentsize < phdr_size)
        return ELF_E_BADPH;
    if (phoff > len || (UINT64)phnum * phentsize > len - phoff)
        return ELF_E_TRUNC;

    UINT64 top = 0, loaded = 0;
    for (UINTN i = 0; i < phnum; i++) {
        const UINT8 *rec = img + (UINTN)phoff + i * phentsize;

        UINT32 ptype;
        UINT64 off, vaddr, filesz, memsz;
        if (is64) {
            elf_phdr64_t p;
            memcpy(&p, rec, sizeof(p));
            ptype = p.p_type; off = p.p_offset; vaddr = p.p_vaddr;
            filesz = p.p_filesz; memsz = p.p_memsz;
        } else {
            elf_phdr32_t p;
            memcpy(&p, rec, sizeof(p));
            ptype = p.p_type; off = p.p_offset; vaddr = p.p_vaddr;
            filesz = p.p_filesz; memsz = p.p_memsz;
        }

        if (ptype != PT_LOAD)
            continue;
        loaded++;
        if (filesz > memsz)
            return ELF_E_BADPH;
        if (off > len || filesz > len - off)
            return ELF_E_TRUNC;
        UINT64 dend = vaddr + memsz;
        if (dend < vaddr)
            return ELF_E_RANGE;     /* wraps */

        UINT8 *dst = cb->reserve(ctx, vaddr, memsz);
        if (!dst)
            return ELF_E_NOMEM;
        memcpy(dst, img + (UINTN)off, (UINTN)filesz);
        if (filesz < memsz) {
            UINTN zero_len = (UINTN)(memsz - filesz);
            if (zero_len > 4096) zero_len = 4096;
            memset(dst + (UINTN)filesz, 0, zero_len);
        }

        if (dend > top)
            top = dend;
    }

    if (loaded == 0)
        return ELF_E_NOSEG;
    *out_entry = entry;
    if (out_top)
        *out_top = top;
    return ELF_OK;
}

INT32 elf_load(const UINT8 *img, UINTN len, const elf_callbacks_t *cb,
               VOID *ctx, UINT64 *out_entry, UINT64 *out_top)
{
    if (!img || len < 16 || !cb || !cb->reserve || !out_entry)
        return ELF_E_ARGS;
    if (img[0] != ELF_MAGIC0 || img[1] != 'E' ||
        img[2] != 'L' || img[3] != 'F')
        return ELF_E_MAGIC;
    if (img[5] != ELF_DATA2LSB)
        return ELF_E_ENDIAN;

    INT32 rc;
    switch (img[4]) {
    case ELF_CLASS64: rc = load_class(img, len, 1, cb, ctx, out_entry, out_top); break;
    case ELF_CLASS32: rc = load_class(img, len, 0, cb, ctx, out_entry, out_top); break;
    default:
        return ELF_E_CLASS;
    }
    return rc;
}

const char *elf_strerror(INT32 err)
{
    switch (err) {
    case ELF_OK:       return "ok";
    case ELF_E_ARGS:   return "bad arguments";
    case ELF_E_MAGIC:  return "not an ELF image";
    case ELF_E_CLASS:  return "unknown ELF class";
    case ELF_E_ENDIAN: return "big-endian images unsupported";
    case ELF_E_ARCH:   return "wrong architecture";
    case ELF_E_TRUNC:  return "image truncated";
    case ELF_E_NOSEG:  return "no loadable segments";
    case ELF_E_BADPH:  return "bad program headers";
    case ELF_E_RANGE:  return "segment address overflow";
    case ELF_E_NOMEM:  return "allocator refused segment";
    default:           return "unknown error";
    }
}
