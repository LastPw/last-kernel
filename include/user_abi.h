#ifndef LASTOS_USER_ABI_H
#define LASTOS_USER_ABI_H

/* int 0x80 ABI for 32-bit user programs. Arguments are in ebx/ecx/edx/esi. */
#define LASTOS_SYS_WRITE_STR 2
#define LASTOS_SYS_EXIT      18
#define LASTOS_SYS_OPEN      11
#define LASTOS_SYS_CLOSE     12
#define LASTOS_SYS_READ      13
#define LASTOS_SYS_WRITE     14
#define LASTOS_SYS_SEEK      15
#define LASTOS_SYS_DUP       16
#define LASTOS_SYS_STAT      17

#endif
