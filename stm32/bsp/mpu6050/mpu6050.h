#ifndef BSP_MPU6050_H
#define BSP_MPU6050_H

#include "stm32f4xx_hal.h"

/* GY-521 / MPU6050: verify WHO_AM_I (0x68 MPU6050, 0x70 MPU6500 clone die),
 * wake, +-4 g accel range (+-500 dps gyro).
 * Returns 0 on success, -1 on timeout/NACK/ID mismatch (increments ERR_MPU_TIMEOUT). */
int mpu6050_init(I2C_HandleTypeDef *hi2c);

/* Burst-read accel (g). Blocking <= 100 ms. Returns 0 on success.
 * On failure returns -1. Implausible samples increment ERR_SENSOR_RANGE and
 * return the last-good values with -1. */
int mpu6050_read(float *ax_g, float *ay_g, float *az_g);

#endif /* BSP_MPU6050_H */
