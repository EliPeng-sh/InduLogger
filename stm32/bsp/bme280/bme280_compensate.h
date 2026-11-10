#ifndef BME280_COMPENSATE_H
#define BME280_COMPENSATE_H

/* Pure compensation math for the BME280 (datasheet 4.2.3, Bosch reference).
 * HAL-free so it is host-unit-testable (host/tests/test_bme280_compensate.c);
 * the hardware seam (I2C burst read, trim parse) stays in bme280.c.
 */

#include <stdint.h>

typedef struct {
  uint16_t T1;
  int16_t  T2, T3;
  uint16_t P1;
  int16_t  P2, P3, P4, P5, P6, P7, P8, P9;
  uint8_t  H1, H3;
  int16_t  H2, H4, H5;
  int8_t   H6;
} bme280_trim_t;

/* Compensate raw temperature. Returns centi-degrees C (25.08 C -> 2508) and
 * writes the fine-resolution t_fine needed by pressure/humidity. */
int32_t bme280_compensate_temp(const bme280_trim_t *tr, int32_t adc_T, int32_t *t_fine);

/* Compensate raw pressure using t_fine from bme280_compensate_temp.
 * Returns hPa. Returns 0.0f if the trims are degenerate (dig_P1 == 0). */
float bme280_compensate_press(const bme280_trim_t *tr, int32_t adc_P, int32_t t_fine);

/* Compensate raw humidity using t_fine. Returns %rH clamped to 0..100. */
float bme280_compensate_hum(const bme280_trim_t *tr, int32_t adc_H, int32_t t_fine);

#endif /* BME280_COMPENSATE_H */
