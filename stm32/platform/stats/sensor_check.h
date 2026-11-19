#ifndef PLATFORM_SENSOR_CHECK_H
#define PLATFORM_SENSOR_CHECK_H

#include <stdbool.h>

/* Plausibility windows: BME280 datasheet operating ranges / physical sanity. Host-tested. */
bool sensor_plausible_bme(float t_c, float h_pct, float p_hpa);  /* -40..85 C, 0..100 %, 300..1100 hPa */
bool sensor_plausible_mpu(float ax_g, float ay_g, float az_g);   /* magnitude 0.3..2.0 g */

#endif /* PLATFORM_SENSOR_CHECK_H */
