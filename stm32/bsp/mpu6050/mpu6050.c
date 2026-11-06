#include "bsp/mpu6050/mpu6050.h"
#include "platform/stats/sensor_check.h"
#include "platform/stats/stats.h"

#include <stdint.h>
#include <stdio.h>

#define MPU_REG_WHO_AM_I  0x75u
#define MPU_REG_PWR_MGMT1 0x6Bu
#define MPU_REG_ACCEL_CFG 0x1Cu
#define MPU_REG_GYRO_CFG  0x1Bu
#define MPU_REG_ACCEL_X   0x3Bu   /* burst 0x3B..0x48: accel 6B, temp 2B, gyro 6B */
#define MPU_WHO_AM_I_ID   0x68u
#define MPU_TIMEOUT_MS    100u
#define MPU_LSB_PER_G     8192.0f   /* +-4 g range (AFS_SEL=1) */

static I2C_HandleTypeDef *s_i2c;
static const uint8_t s_addr8 = 0xD0u;   /* 0x68 << 1 */

static float s_last_ax = 0.0f, s_last_ay = 0.0f, s_last_az = 1.0f;  /* last-good */

static int read_reg(uint8_t reg, uint8_t *buf, uint16_t len)
{
  if (HAL_I2C_Mem_Read(s_i2c, s_addr8, reg, I2C_MEMADD_SIZE_8BIT, buf, len,
                       MPU_TIMEOUT_MS) != HAL_OK) {
    stats_error(ERR_MPU_TIMEOUT);
    return -1;
  }
  return 0;
}

static int write_reg(uint8_t reg, uint8_t val)
{
  if (HAL_I2C_Mem_Write(s_i2c, s_addr8, reg, I2C_MEMADD_SIZE_8BIT, &val, 1,
                        MPU_TIMEOUT_MS) != HAL_OK) {
    stats_error(ERR_MPU_TIMEOUT);
    return -1;
  }
  return 0;
}

int mpu6050_init(I2C_HandleTypeDef *hi2c)
{
  uint8_t who = 0;

  s_i2c = hi2c;

  if (read_reg(MPU_REG_WHO_AM_I, &who, 1) != 0) return -1;
  if (who != MPU_WHO_AM_I_ID) {
    /* GY-521 clones occasionally report a different ID; report once. Only add a
     * whitelist value here after confirming the board on the bench is a clone. */
    static int s_id_reported;
    stats_error(ERR_MPU_TIMEOUT);
    if (!s_id_reported) {
      s_id_reported = 1;
      printf("MPU6050: WHO_AM_I=0x%02X, expected 0x68\r\n", (unsigned)who);
    }
    return -1;
  }

  if (write_reg(MPU_REG_PWR_MGMT1, 0x00u) != 0) return -1;   /* wake from sleep */
  if (write_reg(MPU_REG_ACCEL_CFG, 0x08u) != 0) return -1;   /* +-4 g */
  if (write_reg(MPU_REG_GYRO_CFG, 0x08u) != 0) return -1;    /* +-500 dps */
  return 0;
}

int mpu6050_read(float *ax_g, float *ay_g, float *az_g)
{
  uint8_t raw[14];
  int16_t ax, ay, az;
  float gx, gy, gz;

  if (s_i2c == NULL) return -1;
  if (read_reg(MPU_REG_ACCEL_X, raw, (uint16_t)sizeof raw) != 0) return -1;

  ax = (int16_t)(((uint16_t)raw[0] << 8) | raw[1]);
  ay = (int16_t)(((uint16_t)raw[2] << 8) | raw[3]);
  az = (int16_t)(((uint16_t)raw[4] << 8) | raw[5]);
  gx = (float)ax / MPU_LSB_PER_G;
  gy = (float)ay / MPU_LSB_PER_G;
  gz = (float)az / MPU_LSB_PER_G;

  if (!sensor_plausible_mpu(gx, gy, gz)) {
    stats_error(ERR_SENSOR_RANGE);
    *ax_g = s_last_ax;
    *ay_g = s_last_ay;
    *az_g = s_last_az;
    return -1;
  }

  s_last_ax = gx;
  s_last_ay = gy;
  s_last_az = gz;
  *ax_g = gx;
  *ay_g = gy;
  *az_g = gz;
  return 0;
}
