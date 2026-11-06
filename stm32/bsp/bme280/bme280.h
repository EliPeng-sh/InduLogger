#ifndef BSP_BME280_H
#define BSP_BME280_H

#include "stm32f4xx_hal.h"

/* Probe (0x76 then 0x77), verify chip id 0x60, load calibration, go normal mode.
 * Returns 0 on success, -1 on timeout/NACK (each failure increments ERR_BME_TIMEOUT). */
int bme280_init(I2C_HandleTypeDef *hi2c);

/* Burst-read T/P/H. Blocking <= 100 ms. Returns 0 on success, -1 on failure. */
int bme280_read(float *temp_c, float *hum_pct, float *press_hpa);

#endif /* BSP_BME280_H */
