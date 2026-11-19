#ifndef BSP_MPU6050_ID_H
#define BSP_MPU6050_ID_H

#include <stdint.h>

/* WHO_AM_I whitelist for the GY-521 bench board. Board-measured: the module
 * carries an MPU6500 die reporting 0x70 (register-compatible with MPU6050 for
 * WHO_AM_I/PWR_MGMT1/ACCEL_CFG/GYRO_CFG/ACCEL_XOUT used by this driver).
 * 0x68 = genuine MPU6050. Anything else is rejected fail-fast. */
int mpu_id_accepted(uint8_t who_am_i);

#endif /* BSP_MPU6050_ID_H */
