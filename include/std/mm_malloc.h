/* Freestanding override of gcc's <mm_malloc.h> for the BearSSL static-lib
   build.  gcc's version is a hosted header (declares malloc/free and pulls
   nothing else here), but we only need the _mm_malloc/_mm_free prototypes.
   The x86 intrinsics path that calls them (#if BR_AES_X86NI) is dormant on a
   plain i686 build, so no kernel allocator backs them. */
#ifndef LASTOS_FS_MM_MALLOC_H
#define LASTOS_FS_MM_MALLOC_H

void* _mm_malloc(size_t size, size_t align);
void  _mm_free(void* ptr);

#endif
