#include "bsp/bme280/bme280.h"
#include "bsp/bme280/bme280_compensate.h"
#include "platform/stats/stats.h"

#include <stdint.h>

#define BME_REG_ID        0xD0u
#define BME_REG_RESET     0xE0u
#define BME_REG_CALIB     0x88u   /* T1-T3, P1-P9: 0x88..0x9F (+ 0xA1 H1 in the same burst) */
#define BME_REG_CALIB_H   0xE1u   /* H2-H6 */
#define BME_REG_CONFIG    0xF5u
#define BME_REG_CTRL_HUM  0xF2u
#define BME_REG_CTRL_MEAS 0xF4u
#define BME_REG_DATA      0xF7u   /* burst: P[0..2] T[3..5] H[6..7] */
#define BME_CHIP_ID       0x60u   /* BMP280 would be 0x58 */
#define BME_TIMEOUT_MS    100u

static I2C_HandleTypeDef *s_i2c;
static uint8_t s_addr8;   /* 8-bit (shifted) device address */

static bme280_trim_t s_trim;

static int read_reg(uint8_t reg, uint8_t *buf, uint16_t len)
{
  if (HAL_I2C_Mem_Read(s_i2c, s_addr8, reg, I2C_MEMADD_SIZE_8BIT, buf, len,
                       BME_TIMEOUT_MS) != HAL_OK) {
    stats_error(ERR_BME_TIMEOUT);
    return -1;
  }
  return 0;
}

static int write_reg(uint8_t reg, uint8_t val)
{
  if (HAL_I2C_Mem_Write(s_i2c, s_addr8, reg, I2C_MEMADD_SIZE_8BIT, &val, 1,
                        BME_TIMEOUT_MS) != HAL_OK) {
    stats_error(ERR_BME_TIMEOUT);
    return -1;
  }
  return 0;
}

int bme280_init(I2C_HandleTypeDef *hi2c)
{
  static const uint8_t addrs[2] = { 0xECu, 0xEEu }; /* 0x76, 0x77 shifted */
  uint8_t id = 0;
  uint8_t calib[26];   /* 0x88..0xA1: trims + reserved 0xA0 + H1 */
  uint8_t calib_h[7];  /* 0xE1..0xE7 */

  s_i2c = hi2c;

  s_addr8 = addrs[0];
  if (read_reg(BME_REG_ID, &id, 1) != 0 || id != BME_CHIP_ID) {
    s_addr8 = addrs[1];
    if (read_reg(BME_REG_ID, &id, 1) != 0 || id != BME_CHIP_ID) {
      return -1;
    }
  }

  if (write_reg(BME_REG_RESET, 0xB6u) != 0) return -1;
  HAL_Delay(10);   /* datasheet: max 2 ms after soft reset */

  if (read_reg(BME_REG_CALIB, calib, (uint16_t)sizeof calib) != 0) return -1;
  s_trim.T1 = (uint16_t)(calib[0] | (calib[1] << 8));
  s_trim.T2 = (int16_t)(calib[2] | (calib[3] << 8));
  s_trim.T3 = (int16_t)(calib[4] | (calib[5] << 8));
  s_trim.P1 = (uint16_t)(calib[6] | (calib[7] << 8));
  s_trim.P2 = (int16_t)(calib[8]  | (calib[9]  << 8));
  s_trim.P3 = (int16_t)(calib[10] | (calib[11] << 8));
  s_trim.P4 = (int16_t)(calib[12] | (calib[13] << 8));
  s_trim.P5 = (int16_t)(calib[14] | (calib[15] << 8));
  s_trim.P6 = (int16_t)(calib[16] | (calib[17] << 8));
  s_trim.P7 = (int16_t)(calib[18] | (calib[19] << 8));
  s_trim.P8 = (int16_t)(calib[20] | (calib[21] << 8));
  s_trim.P9 = (int16_t)(calib[22] | (calib[23] << 8));
  s_trim.H1 = calib[25];

  if (read_reg(BME_REG_CALIB_H, calib_h, (uint16_t)sizeof calib_h) != 0) return -1;
  s_trim.H2 = (int16_t)(calib_h[0] | (calib_h[1] << 8));
  s_trim.H3 = calib_h[2];
  s_trim.H4 = (int16_t)(((int8_t)calib_h[3] << 4) | (calib_h[4] & 0x0Fu));
  s_trim.H5 = (int16_t)(((int8_t)calib_h[5] << 4) | (calib_h[4] >> 4));
  s_trim.H6 = (int8_t)calib_h[6];

  /* t_sb = 500 ms (matches sampling), IIR x16; hum x1 (must precede ctrl_meas);
   * T x2, P x16, normal mode */
  if (write_reg(BME_REG_CONFIG, 0x9Cu) != 0) return -1;
  if (write_reg(BME_REG_CTRL_HUM, 0x01u) != 0) return -1;
  if (write_reg(BME_REG_CTRL_MEAS, 0x57u) != 0) return -1;

  return 0;
}

int bme280_read(float *temp_c, float *hum_pct, float *press_hpa)
{
  uint8_t raw[8];
  int32_t adc_T, adc_P, adc_H;
  int32_t t_fine;

  if (s_i2c == NULL) return -1;
  if (read_reg(BME_REG_DATA, raw, (uint16_t)sizeof raw) != 0) return -1;

  adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
  adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
  adc_H = ((int32_t)raw[6] << 8) | raw[7];

  *temp_c = (float)bme280_compensate_temp(&s_trim, adc_T, &t_fine) / 100.0f;
  *press_hpa = bme280_compensate_press(&s_trim, adc_P, t_fine);
  *hum_pct = bme280_compensate_hum(&s_trim, adc_H, t_fine);

  return 0;
}

/* >4 us at 100 MHz; exact duration not timing-critical here (far above the
 * I2C standard-mode minimum pulse width). */
static void bus_delay(void)
{
  for (volatile uint32_t d = 0; d < 5000u; d++) {
  }
}

void bme280_bus_recover(void)
{
  GPIO_InitTypeDef io = {0};
  const uint32_t scl = GPIO_PIN_8;  /* PB8 = I2C1 SCL, fixed by MSP */
  const uint32_t sda = GPIO_PIN_9;  /* PB9 = I2C1 SDA, fixed by MSP */
  int i;

  io.Pin = scl | sda;
  io.Mode = GPIO_MODE_OUTPUT_OD;
  io.Pull = GPIO_NOPULL;
  io.Speed = GPIO_SPEED_FREQ_LOW;
  io.Alternate = 0;
  HAL_GPIO_Init(GPIOB, &io);
  HAL_GPIO_WritePin(GPIOB, scl | sda, GPIO_PIN_SET); /* release both lines */

  /* Up to 9 SCL pulses: a stuck slave can be waiting for at most one byte + ACK.
   * Stop early once the slave releases SDA. */
  for (i = 0; i < 9; i++) {
    bus_delay();
    HAL_GPIO_WritePin(GPIOB, scl, GPIO_PIN_RESET);
    bus_delay();
    HAL_GPIO_WritePin(GPIOB, scl, GPIO_PIN_SET);
    if (HAL_GPIO_ReadPin(GPIOB, sda) == GPIO_PIN_SET) {
      break;
    }
  }

  /* STOP condition: SDA falling edge while SCL high. */
  bus_delay();
  HAL_GPIO_WritePin(GPIOB, sda, GPIO_PIN_RESET);
  bus_delay();
  HAL_GPIO_WritePin(GPIOB, sda, GPIO_PIN_SET);
  bus_delay();

  /* Restore AF open-drain exactly as stm32f4xx_hal_msp.c configures it. */
  io.Mode = GPIO_MODE_AF_OD;
  io.Pull = GPIO_NOPULL;
  io.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  io.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB, &io);
}
