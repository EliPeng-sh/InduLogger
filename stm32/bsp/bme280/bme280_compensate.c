#include "bsp/bme280/bme280_compensate.h"

/* Bosch BME280 datasheet 4.2.3 compensation, integer T/P (64-bit) + float H.
 * Transcribed from the datasheet / Bosch BME280_driver reference. */

int32_t bme280_compensate_temp(const bme280_trim_t *tr, int32_t adc_T, int32_t *t_fine)
{
  int64_t d1 = (int64_t)(adc_T >> 3) - ((int64_t)tr->T1 << 1);
  int64_t d2 = (int64_t)(adc_T >> 4) - (int64_t)tr->T1;
  int32_t v1 = (int32_t)((d1 * (int64_t)tr->T2) >> 11);
  int32_t v2 = (int32_t)(((d2 * d2) >> 12) * (int64_t)tr->T3 >> 14);
  *t_fine = v1 + v2;
  return (*t_fine * 5 + 128) >> 8;   /* centi-degrees C */
}

float bme280_compensate_press(const bme280_trim_t *tr, int32_t adc_P, int32_t t_fine)
{
  int64_t v1 = ((int64_t)t_fine) - 128000;
  int64_t v2 = v1 * v1 * (int64_t)tr->P6;
  v2 = v2 + ((v1 * (int64_t)tr->P5) << 17);
  v2 = v2 + ((int64_t)tr->P4 << 35);
  v1 = ((v1 * v1 * (int64_t)tr->P3) >> 8) + ((v1 * (int64_t)tr->P2) << 12);
  v1 = (((((int64_t)1) << 47) + v1) * (int64_t)tr->P1) >> 33;
  if (v1 == 0) {
    return 0.0f;   /* P1 == 0: fail-safe, no division */
  }
  {
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - v2) * 3125) / v1;
    v1 = ((int64_t)tr->P9 * (p >> 13) * (p >> 13)) >> 25;
    v2 = ((int64_t)tr->P8 * p) >> 19;
    /* Bosch returns Pa x 256 (Q24.8): append P7 (its own <<4 term is part of
     * the Q24.8 value), then /25600 brings it to hPa (256 -> Pa, /100 -> hPa). */
    {
      int64_t pf = ((p + v1 + v2) >> 8) + ((int64_t)tr->P7 << 4);
      return (float)pf / 25600.0f;
    }
  }
}

float bme280_compensate_hum(const bme280_trim_t *tr, int32_t adc_H, int32_t t_fine)
{
  float var_h = (float)t_fine - 76800.0f;
  float hum;
  var_h = (((float)adc_H) - ((float)tr->H4 * 64.0f + (float)tr->H5 / 16384.0f * var_h)) *
          ((float)tr->H2 / 65536.0f *
           (1.0f + (float)tr->H6 / 67108864.0f * var_h *
                   (1.0f + (float)tr->H3 / 67108864.0f * var_h)));
  hum = var_h * (1.0f - (float)tr->H1 * var_h / 524288.0f);
  if (hum > 100.0f) hum = 100.0f;
  if (hum < 0.0f) hum = 0.0f;
  return hum;
}
