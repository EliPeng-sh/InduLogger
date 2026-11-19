#include "bsp/sdcard/user_diskio_glue.h"
#include "bsp/sdcard/sd_spi.h"
#include "ff_gen_drv.h"

#include <stdint.h>

/* Thin FATFS <-> sd_spi mapping. Error COUNTING lives in sd_spi.c (one
 * stats_error per failed SPI operation); this layer only translates return
 * codes. All underlying operations are 100 ms bounded, so a missing or dead
 * card surfaces as an error return instead of a hang. */

static int s_inited; /* set once sd_spi_init() succeeds */

DSTATUS sd_disk_initialize(BYTE pdrv)
{
  (void)pdrv;
  s_inited = (sd_spi_init() == 0);
  return s_inited ? (DSTATUS)0 : STA_NOINIT;
}

DSTATUS sd_disk_status(BYTE pdrv)
{
  (void)pdrv;
  return s_inited ? (DSTATUS)0 : STA_NOINIT;
}

/* Any failed block I/O invalidates the init state: the card lost power (hot
 * unplug) or wedged, and a freshly re-seated card always needs the full
 * CMD0/ACMD41 init sequence again.
 *
 * TWO layers must be cleared. (1) s_inited here feeds sd_disk_status, which
 * find_volume consults before re-mounting. (2) ST's diskio.c caches the init
 * result in disk.is_initialized[] and short-circuits disk_initialize while it
 * is set — and FatFs never clears it. Board-evidenced failure mode with only
 * (1): every re-mount "initialized" from the cache, hit the un-initialized
 * card with CMD17, and died with FR_DISK_ERR forever (glue init counter
 * frozen at 1, stage-9 diag, FRESULT=1). */
extern Disk_drvTypeDef disk; /* defined in ff_gen_drv.c; same extern diskio.c uses */

static void io_failed_invalidate(void)
{
  s_inited = 0;
  disk.is_initialized[0] = 0; /* single drive (pdrv 0); force disk_initialize to re-run */
}

DRESULT sd_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
  (void)pdrv;
  for (UINT i = 0; i < count; i++) {
    uint8_t *blk = buff + (size_t)i * SD_SECTOR_SIZE;
    if (sd_spi_read_single((uint32_t)sector + (uint32_t)i, blk) != 0) {
      io_failed_invalidate();
      return RES_ERROR;
    }
  }
  return RES_OK;
}

DRESULT sd_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
  (void)pdrv;
  for (UINT i = 0; i < count; i++) {
    const uint8_t *blk = buff + (size_t)i * SD_SECTOR_SIZE;
    if (sd_spi_write_single((uint32_t)sector + (uint32_t)i, blk) != 0) {
      io_failed_invalidate();
      return RES_ERROR;
    }
  }
  return RES_OK;
}

DRESULT sd_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  (void)pdrv;
  (void)buff; /* only CTRL_SYNC is supported; no output parameters needed */
  switch (cmd) {
  case CTRL_SYNC:
    /* Single-block writes already wait out the card busy state, so there is
     * no deferred write queue to flush. */
    return RES_OK;
  case GET_SECTOR_COUNT:
  case GET_BLOCK_SIZE:
    /* Only needed for f_mkfs/f_expand; cards are pre-formatted on the host. */
    return RES_PARERR;
  case CTRL_TRIM:
    return RES_OK;
  default:
    return RES_PARERR;
  }
}
