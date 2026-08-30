/* Multiboot2 information structure builder.
   Fills a caller-provided buffer with the fixed header + tags; all
   offsets 8-byte aligned per spec. */
#ifndef PULSE_MB2_H
#define PULSE_MB2_H

#include "pulsebase.h"
#include "memmap.h"

#define MB2_BOOTLOADER_MAGIC 0x36D76289U

typedef struct {
    UINT8 *buf;
    UINTN cap;
    UINTN len;
} mb2_builder_t;

void mb2_init(mb2_builder_t *mb, VOID *buf, UINTN cap);
void mb2_tag_string(mb2_builder_t *mb, UINT32 type, const char *s);
void mb2_tag_memmap(mb2_builder_t *mb, const pulse_memmap_t *mm);
/* writes end tag + total size header; returns total MBI size (0 = no room) */
UINTN mb2_finish(mb2_builder_t *mb);

#endif
