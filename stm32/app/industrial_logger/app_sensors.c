#include "app/industrial_logger/app_sensors.h"
#include "bsp/bme280/bme280.h"
#include "platform/stats/stats.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

extern I2C_HandleTypeDef hi2c1;

static int s_inited;

void app_sensors_tick(void)
{
  float t, h, p;

  if (!s_inited) {
    if (bme280_init(&hi2c1) == 0) {
      s_inited = 1;
      printf("BME280 init ok\r\n");
    } else {
      printf("BME280 init failed (err cnt=%u)\r\n",
             (unsigned)stats_get(ERR_BME_TIMEOUT));
    }
    return;
  }

  if (bme280_read(&t, &h, &p) == 0)
    printf("BME280 t=%.2fC rh=%.2f%% p=%.1fhPa\r\n", t, h, p);
  else
    printf("BME280 read failed (err cnt=%u)\r\n",
           (unsigned)stats_get(ERR_BME_TIMEOUT));

  printf("wm=%u\r\n", (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
