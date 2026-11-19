#ifndef BSP_SDCARD_USER_DISKIO_GLUE_H
#define BSP_SDCARD_USER_DISKIO_GLUE_H

#include "diskio.h"

#include <stdint.h>

/* FATFS diskio glue for the SPI SD card (bsp/sdcard/sd_spi.c). Called from the
 * CubeMX user_diskio.c USER CODE sections, which survive regeneration; this
 * file holds the real implementation so CubeMX regen stays a no-op for us. */

DSTATUS sd_disk_initialize(BYTE pdrv);
DSTATUS sd_disk_status(BYTE pdrv);
DRESULT sd_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
DRESULT sd_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
DRESULT sd_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

#endif /* BSP_SDCARD_USER_DISKIO_GLUE_H */
