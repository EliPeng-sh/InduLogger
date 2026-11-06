#include "bsp/bme280/bme280.h"
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

static uint16_t dig_T1, dig_P1;
static int16_t  dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static uint8_t  dig_H1, dig_H3;
static int16_t  dig_H2, dig_H4, dig_H5;
static int8_t   dig_H6;

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
  dig_T1 = (uint16_t)(calib[0] | (calib[1] << 8));
  dig_T2 = (int16_t)(calib[2] | (calib[3] << 8));
  dig_T3 = (int16_t)(calib[4] | (calib[5] << 8));
  dig_P1 = (uint16_t)(calib[6] | (calib[7] << 8));
  dig_P2 = (int16_t)(calib[8]  | (calib[9]  << 8));
  dig_P3 = (int16_t)(calib[10] | (calib[11] << 8));
  dig_P4 = (int16_t)(calib[12] | (calib[13] << 8));
  dig_P5 = (int16_t)(calib[14] | (calib[15] << 8));
  dig_P6 = (int16_t)(calib[16] | (calib[17] << 8));
  dig_P7 = (int16_t)(calib[18] | (calib[19] << 8));
  dig_P8 = (int16_t)(calib[20] | (calib[21] << 8));
  dig_P9 = (int16_t)(calib[22] | (calib[23] << 8));
  dig_H1 = calib[25];

  if (read_reg(BME_REG_CALIB_H, calib_h, (uint16_t)sizeof calib_h) != 0) return -1;
  dig_H2 = (int16_t)(calib_h[0] | (calib_h[1] << 8));
  dig_H3 = calib_h[2];
  dig_H4 = (int16_t)(((int8_t)calib_h[3] << 4) | (calib_h[4] & 0x0Fu));
  dig_H5 = (int16_t)(((int8_t)calib_h[5] << 4) | (calib_h[4] >> 4));
  dig_H6 = (int8_t)calib_h[6];

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
  float var_h, hum;

  if (s_i2c == NULL) return -1;
  if (read_reg(BME_REG_DATA, raw, (uint16_t)sizeof raw) != 0) return -1;

  adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
  adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
  adc_H = ((int32_t)raw[6] << 8) | raw[7];

  /* Temperature, integer (datasheet 4.2.3); t_fine scale ~45.6 x century-C */
  {
    int64_t d1 = (int64_t)(adc_T >> 3) - ((int64_t)dig_T1 << 1);
    int64_t d2 = (int64_t)(adc_T >> 4) - (int64_t)dig_T1;
    int32_t v1 = (int32_t)((d1 * (int64_t)dig_T2) >> 11);
    int32_t v2 = (int32_t)(((d2 * d2) >> 12) * (int64_t)dig_T3 >> 14);
    t_fine = v1 + v2;
  }
  *temp_c = (float)((t_fine * 5 + 128) >> 8) / 100.0f;

  /* Pressure, 64-bit integer (datasheet 4.2.3) */
  {
    int64_t v1 = ((int64_t)t_fine) - 128000;
    int64_t v2 = v1 * v1 * (int64_t)dig_P6;
    v2 = v2 + ((v1 * (int64_t)dig_P5) << 17);
    v2 = v2 + ((int64_t)dig_P4 << 35);
    v1 = ((v1 * v1 * (int64_t)dig_P3) >> 8) + ((v1 * (int64_t)dig_P2) << 12);
    v1 = (((((int64_t)1) << 47) + v1) * (int64_t)dig_P1) >> 33;
    if (v1 == 0) {
      *press_hpa = 0.0f;   /* P1 == 0: fail-safe, no division */
    } else {
      int64_t p = 1048576 - adc_P;
      p = (((p << 31) - v2) * 3125) / v1;
      v1 = ((int64_t)dig_P9 * (p >> 13) * (p >> 13)) >> 25;
      v2 = ((int64_t)dig_P8 * p) >> 19;
      *press_hpa = (float)((p + v1 + v2) >> 8) / 100.0f;   /* Pa -> hPa */
    }
  }

  /* Humidity (datasheet 4.2.3, float; consumes the t_fine above) */
  var_h = (float)t_fine - 76800.0f;
  var_h = (((float)adc_H) - ((float)dig_H4 * 64.0f + (float)dig_H5 / 16384.0f * var_h)) *
          ((float)dig_H2 / 65536.0f *
           (1.0f + (float)dig_H6 / 67108864.0f * var_h *
                   (1.0f + (float)dig_H3 / 67108864.0f * var_h)));
  hum = var_h * (1.0f - (float)dig_H1 * var_h / 524288.0f);
  if (hum > 100.0f) hum = 100.0f;
  if (hum < 0.0f) hum = 0.0f;
  *hum_pct = hum;

  return 0;
}
