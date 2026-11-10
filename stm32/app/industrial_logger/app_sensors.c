#include "app/industrial_logger/app_sensors.h"
#include "bsp/bme280/bme280.h"
#include "bsp/bme280/bme280_recovery.h"
#include "bsp/mpu6050/mpu6050.h"
#include "platform/diag/diag.h"
#include "platform/stats/stats.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

extern I2C_HandleTypeDef hi2c1;

/* After this many consecutive read failures, assume the slave I2C FSM is wedged
 * (board-proven failure mode: it holds SDA low, HAL never recovers) and run the
 * AN4235 GPIO bus-clear + re-init. 3 keeps recovery under ~8 s at the 2 s tick. */
#define BME_RECOVER_AFTER_FAILS 3u

static int s_bme_inited;
static int s_mpu_inited;

static bme280_recovery_t s_bme_recov;
static int s_bme_recov_ready;

static void bme_recov_once(void)
{
  if (!s_bme_recov_ready) {
    bme280_recovery_init(&s_bme_recov, BME_RECOVER_AFTER_FAILS);
    s_bme_recov_ready = 1;
  }
}

void app_sensors_tick(void)
{
  float t, h, p, ax, ay, az;

  if (!s_bme_inited) {
    diag_bench_gpio_set(1, true); /* BENCH_GPIO1 high for the whole I2C attempt (scope) */
    uint32_t t0 = diag_cycles();
    int rc = bme280_init(&hi2c1);
    uint32_t us = (diag_cycles() - t0) / 100u;
    diag_bench_gpio_set(1, false);
    if (rc == 0) {
      bme_recov_once();
      bme280_recovery_on_success(&s_bme_recov);
      s_bme_inited = 1;
      printf("BME280 init ok (%u us)\r\n", (unsigned)us);
    } else {
      printf("BME280 init failed (err cnt=%u, %u us)\r\n",
             (unsigned)stats_get(ERR_BME_TIMEOUT), (unsigned)us);
    }
  } else {
    diag_bench_gpio_set(1, true);
    uint32_t t0 = diag_cycles();
    int rc = bme280_read(&t, &h, &p);
    uint32_t us = (diag_cycles() - t0) / 100u;
    diag_bench_gpio_set(1, false);
    if (rc == 0) {
      bme280_recovery_on_success(&s_bme_recov);
      printf("BME280 t=%.2fC rh=%.2f%% p=%.1fhPa (i2c read: %u us)\r\n",
             t, h, p, (unsigned)us);
    } else {
      printf("BME280 read failed (err cnt=%u, %u us)\r\n",
             (unsigned)stats_get(ERR_BME_TIMEOUT), (unsigned)us);
      if (bme280_recovery_on_fail(&s_bme_recov)) {
        printf("BME280 bus recover: 9xSCL+STOP (SDA stuck), re-init next tick\r\n");
        bme280_bus_recover();
        s_bme_inited = 0; /* next tick re-inits (soft reset clears the slave FSM) */
      }
    }
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
