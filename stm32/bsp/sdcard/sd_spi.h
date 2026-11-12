#ifndef BSP_SD_SPI_H
#define BSP_SD_SPI_H

#include "stm32f4xx_hal.h"

/* SPI-mode SD card driver over SPI1 (PA4 = manual CS). All operations are
 * bounded: every HAL transfer uses a 100 ms timeout and every response wait
 * is tick-bounded, so a stuck or removed card bails out instead of hanging
 * (same rule as the I2C drivers). Any failure increments ERR_SD_WRITE. */

#define SD_SECTOR_SIZE 512u

/* Full SPI-mode card init: >=74 clocks, CMD0, CMD8 0x1AA (SDv2), ACMD41(HCS),
 * CMD58 OCR (CCS -> block addressing), CMD59 CRC-off; 400 kHz init speed then
 * 6.25 MHz (APB2 100 MHz / 16; 12.5 MHz proved marginal on dupont wiring).
 * Returns 0 on success, -1 on failure. */
int sd_spi_init(void);

/* Read/write one 512-byte block. lba is always a sector index; byte addressing
 * (SDSC) is translated internally. Returns 0 on success, -1 on failure. */
int sd_spi_read_single(uint32_t lba, uint8_t buf[SD_SECTOR_SIZE]);
int sd_spi_write_single(uint32_t lba, const uint8_t buf[SD_SECTOR_SIZE]);

#endif /* BSP_SD_SPI_H */
