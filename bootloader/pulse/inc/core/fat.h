/* Read-only FAT12/16/32 driver behind the vfs.h contract.
   Mounts at an arbitrary base LBA (partition start). All reads go
   through the caller's blkdev; one 4 KiB bounce buffer per mount. */
#ifndef PULSE_FAT_H
#define PULSE_FAT_H

#include "vfs.h"

extern const fs_driver_t fat_driver;

#endif
