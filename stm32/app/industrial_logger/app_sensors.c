#include "app/industrial_logger/app_sensors.h"
#include "bsp/bme280/bme280.h"
#include "bsp/mpu6050/mpu6050.h"
#include "platform/stats/stats.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

extern I2C_HandleTypeDef hi2c1;

static int s_bme_inited;
static int s_mpu_inited;

void app_sensors_tick(void)
{
  float t, h, p, ax, ay, az;

  if (!s_bme_inited) {
    if (bme280_init(&hi2c1) == 0) {
      s_bme_inited = 1;
      printf("BME280 init ok\r\n");
    } else {
      printf("BME280 init failed (err cnt=%u)\r\n",
             (unsigned)stats_get(ERR_BME_TIMEOUT));
    }
  } else if (bme280_read(&t, &h, &p) == 0) {
    printf("BME280 t=%.2fC rh=%.2f%% p=%.1fhPa\r\n", t, h, p);
  } else {
    printf("BME280 read failed (err cnt=%u)\r\n",
           (unsigned)stats_get(ERR_BME_TIMEOUT));
  }

  if (!s_mpu_inited) {
    if (mpu6050_init(&hi2c1) == 0) {
      s_mpu_inited = 1;
      printf("MPU6050 init ok\r\n");
    } else {
      printf("MPU6050 init failed (err cnt=%u)\r\n",
             (unsigned)stats_get(ERR_MPU_TIMEOUT));
    }
  } else if (mpu6050_read(&ax, &ay, &az) == 0) {
    printf("MPU6050 ax=%.2f ay=%.2f az=%.2f g\r\n", ax, ay, az);
  } else {
    printf("MPU6050 read failed (timeout cnt=%u range cnt=%u)\r\n",
           (unsigned)stats_get(ERR_MPU_TIMEOUT),
           (unsigned)stats_get(ERR_SENSOR_RANGE));
  }

  printf("wm=%u\r\n", (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
