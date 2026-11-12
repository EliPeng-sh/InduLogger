#include "bsp/sdcard/sd_spi.h"
#include "platform/stats/stats.h"

#include <stdint.h>

extern SPI_HandleTypeDef hspi1;

/* SPI-mode SD init sequence per the SD spec part 1 (physical layer):
 * CS high + >=74 clocks -> CMD0 (R1 idle) -> CMD8 0x1AA (SDv2 voltage check,
 * CRC 0x87 fixed) -> ACMD41(HCS) until ready -> CMD58 OCR (CCS bit) ->
 * CMD59 CRC off. CRCs are ignored by cards in SPI mode by default; only CMD0
 * (0x95) and CMD8 (0x87) require valid CRC values. */

#define SD_TIMEOUT_MS    100u
#define SD_CS_PORT       GPIOA
#define SD_CS_PIN        GPIO_PIN_4
#define SD_IDLE_TRIES    64u    /* CMD0 no-card budget: ~30 ms at 400 kHz */
#define SD_READY_TRIES   300u   /* ACMD41 settle budget: ~170 ms at 400 kHz */
#define SD_TOKEN_DATA    0xFEu  /* start token for single-block read/write */
#define SD_RESP_ACCEPT   0x05u  /* data response token xxx00101 = accepted */
#define SD_OCR_CCS_BIT   0x40u  /* OCR[30]: card capacity status (SDHC/SDXC) */

static int s_block_mode; /* 1 = SDHC/SDXC: LBA addressing; 0 = SDSC: byte addr */

static void cs_select(void)
{
  HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
}

static void cs_release(void)
{
  HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
}

/* Full-duplex transfer. HAL has no const on pTxBuffPtr; tx bytes are only read. */
static int spi_xfer(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
  if (HAL_SPI_TransmitReceive(&hspi1, (uint8_t *)(uintptr_t)tx, rx, len,
                              SD_TIMEOUT_MS) != HAL_OK) {
    return -1;
  }
  return 0;
}

static uint8_t spi_byte(uint8_t out)
{
  uint8_t rx = 0xFFu;
  uint8_t tx = out;
  (void)spi_xfer(&tx, &rx, 1u);
  return rx;
}

/* Clock out n idle bytes (DI high); used for init ramp and card MISO release. */
static void spi_clocks(uint16_t n_bytes)
{
  static const uint8_t ff[16] = {
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu
  };
  uint8_t sink[16];
  uint16_t off = 0;
  while (off < n_bytes) {
    uint16_t n = (uint16_t)(n_bytes - off) < (uint16_t)sizeof ff
                     ? (uint16_t)(n_bytes - off)
                     : (uint16_t)sizeof ff;
    (void)spi_xfer(ff, sink, n);
    off = (uint16_t)(off + n);
  }
}

/* Read len bytes (clock 0xFF out in 16-byte chunks). */
static int spi_read(uint8_t *rx, uint16_t len)
{
  static const uint8_t ff[16] = {
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu
  };
  uint16_t off = 0;
  while (off < len) {
    uint16_t n = (uint16_t)(len - off) < (uint16_t)sizeof ff
                     ? (uint16_t)(len - off)
                     : (uint16_t)sizeof ff;
    if (spi_xfer(ff, rx + off, n) != 0) {
      return -1;
    }
    off = (uint16_t)(off + n);
  }
  return 0;
}

/* Write len bytes (discard MISO in 16-byte chunks). */
static int spi_write(const uint8_t *tx, uint16_t len)
{
  uint8_t sink[16];
  uint16_t off = 0;
  while (off < len) {
    uint16_t n = (uint16_t)(len - off) < (uint16_t)sizeof sink
                     ? (uint16_t)(len - off)
                     : (uint16_t)sizeof sink;
    if (spi_xfer(tx + off, sink, n) != 0) {
      return -1;
    }
    off = (uint16_t)(off + n);
  }
  return 0;
}

/* Poll up to 8 bytes for a response byte (0xFF = card busy/not answering). */
static int r1_wait(uint8_t *resp)
{
  for (int i = 0; i < 8; i++) {
    uint8_t b = spi_byte(0xFFu);
    if (b != 0xFFu) {
      *resp = b;
      return 0;
    }
  }
  return -1;
}

/* Send a command and collect its R1 (or R1-portion of R3/R7). */
static int cmd_send(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *r1)
{
  uint8_t req[6] = {
    (uint8_t)(cmd | 0x40u), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
    (uint8_t)(arg >> 8), (uint8_t)arg, crc
  };
  uint8_t echo[6];
  if (spi_xfer(req, echo, (uint16_t)sizeof req) != 0) {
    return -1;
  }
  return r1_wait(r1);
}

static int rx4(uint8_t buf[4])
{
  return spi_read(buf, 4u);
}

/* Wait for a data token, tick-bounded. */
static int data_token_wait(void)
{
  uint32_t t0 = HAL_GetTick();
  for (;;) {
    uint8_t b = spi_byte(0xFFu);
    if (b == SD_TOKEN_DATA) {
      return 0;
    }
    if (b != 0xFFu) {
      return -1; /* error token */
    }
    if ((HAL_GetTick() - t0) > SD_TIMEOUT_MS) {
      return -1;
    }
  }
}

/* Wait while the card holds MISO low (write/erase busy), tick-bounded. */
static int card_busy_wait(void)
{
  uint32_t t0 = HAL_GetTick();
  for (;;) {
    if (spi_byte(0xFFu) == 0xFFu) {
      return 0;
    }
    if ((HAL_GetTick() - t0) > SD_TIMEOUT_MS) {
      return -1;
    }
  }
}

/* Shared failure path: extra clocks, release CS, count the error. */
static int sd_fail(void)
{
  spi_clocks(1);
  cs_release();
  stats_error(ERR_SD_WRITE); /* ERR_SD_WRITE is the general SD-failure counter */
  return -1;
}

/* Set the bus speed by re-initing CR1 (F4 HAL has no baud-rate setter macro).
 * The bus must be idle when this is called. */
static int spi_speed(uint32_t prescaler)
{
  hspi1.Init.BaudRatePrescaler = prescaler;
  return HAL_SPI_Init(&hspi1) == HAL_OK ? 0 : -1;
}

int sd_spi_init(void)
{
  uint8_t r1;
  uint8_t r7[4];

  /* Every init starts from 400 kHz: the SD spec forbids clocking a card
   * above 400 kHz until ACMD41 completes, and after a previous successful
   * init hspi1 still carries the fast data prescaler here (board evidence:
   * re-init at the leftover speed fails at the first commands). */
  if (spi_speed(SPI_BAUDRATEPRESCALER_256) != 0) {
    return sd_fail();
  }

  cs_release();
  spi_clocks(10); /* >= 74 clocks with DI high before any command */

  cs_select();

  /* CMD0 -> idle */
  int idle = 0;
  for (uint32_t i = 0; i < SD_IDLE_TRIES; i++) {
    spi_clocks(1);
    if (cmd_send(0x00u, 0u, 0x95u, &r1) == 0 && r1 == 0x01u) {
      idle = 1;
      break;
    }
  }
  if (!idle) {
    return sd_fail();
  }

  /* CMD8 0x1AA: SDv2 cards echo the argument. SDv1/MMC cards answer illegal
   * command (0x05), and some non-conforming cards answer 0x01 with a wrong
   * echo — both mean "no voltage-info support": fall back to SDv1 init
   * (ACMD41 without HCS; CMD58's CCS bit still decides the addressing). */
  int sdv2 = 0;
  if (cmd_send(0x08u, 0x1AAu, 0x87u, &r1) == 0 && r1 == 0x01u &&
      rx4(r7) == 0) {
    uint32_t echo = ((uint32_t)r7[0] << 24) | ((uint32_t)r7[1] << 16) |
                    ((uint32_t)r7[2] << 8) | (uint32_t)r7[3];
    if (echo == 0x1AAu) {
      sdv2 = 1;
    }
  }

  /* ACMD41 with HCS (SDv2 only) until the card leaves idle state */
  int ready = 0;
  for (uint32_t i = 0; i < SD_READY_TRIES; i++) {
    if (cmd_send(0x37u /* CMD55 */, 0u, 0x01u, &r1) != 0) {
      return sd_fail();
    }
    if (cmd_send(0x29u /* ACMD41 (CMD41) */, sdv2 ? 0x40000000u : 0u, 0x01u,
                 &r1) != 0) {
      return sd_fail();
    }
    if (r1 == 0x00u) {
      ready = 1;
      break;
    }
  }
  if (!ready) {
    return sd_fail();
  }

  /* CMD58: OCR -> CCS bit decides LBA vs byte addressing */
  if (cmd_send(0x3Au, 0u, 0x01u, &r1) != 0 || r1 != 0x00u) {
    return sd_fail();
  }
  if (rx4(r7) != 0) {
    return sd_fail();
  }
  s_block_mode = ((r7[0] & SD_OCR_CCS_BIT) != 0u);

  /* CMD59 arg 0: disable CRC (SPI default, made explicit) */
  if (cmd_send(0x3Bu, 0u, 0x01u, &r1) != 0 || r1 != 0x00u) {
    return sd_fail();
  }

  spi_clocks(1);
  cs_release();

  /* Init done at ~390 kHz; bump to 6.25 MHz (APB2 100 MHz / 16) for data.
   * Ruling (board evidence): 12.5 MHz (prescaler 8) is marginal over dupont
   * wires + the module's level shifters — init passes at 400 kHz but the
   * first data read fails with FR_DISK_ERR. 6.25 MHz keeps a full
   * signal-integrity margin: ~100 us per 512-byte block. */
  if (spi_speed(SPI_BAUDRATEPRESCALER_16) != 0) {
    return sd_fail();
  }

  return 0;
}

int sd_spi_read_single(uint32_t lba, uint8_t buf[SD_SECTOR_SIZE])
{
  uint8_t r1;
  uint8_t crc[2];
  uint32_t addr = s_block_mode ? lba : (lba * SD_SECTOR_SIZE);

  cs_select();
  if (card_busy_wait() != 0) {
    return sd_fail();
  }
  if (cmd_send(0x11u /* CMD17 read single block */, addr, 0x01u, &r1) != 0 ||
      r1 != 0x00u) {
    return sd_fail();
  }
  if (data_token_wait() != 0) {
    return sd_fail();
  }
  if (spi_read(buf, SD_SECTOR_SIZE) != 0 || spi_read(crc, 2u) != 0) {
    return sd_fail();
  }
  spi_clocks(1);
  cs_release();
  return 0;
}

int sd_spi_write_single(uint32_t lba, const uint8_t buf[SD_SECTOR_SIZE])
{
  uint8_t r1;
  uint8_t resp;
  uint8_t tok = SD_TOKEN_DATA;
  uint32_t addr = s_block_mode ? lba : (lba * SD_SECTOR_SIZE);

  cs_select();
  if (card_busy_wait() != 0) {
    return sd_fail();
  }
  if (cmd_send(0x18u /* CMD24 write single block */, addr, 0x01u, &r1) != 0 ||
      r1 != 0x00u) {
    return sd_fail();
  }
  (void)spi_byte(0xFFu); /* one byte gap before the data token */
  if (spi_write(&tok, 1u) != 0 || spi_write(buf, SD_SECTOR_SIZE) != 0 ||
      spi_write((const uint8_t *)"\xFF\xFF", 2u) != 0) {
    return sd_fail();
  }
  resp = (uint8_t)(spi_byte(0xFFu) & 0x1Fu);
  if (resp != SD_RESP_ACCEPT) {
    return sd_fail();
  }
  if (card_busy_wait() != 0) { /* card holds MISO low until flash done */
    return sd_fail();
  }
  spi_clocks(1);
  cs_release();
  return 0;
}
