#include "app/industrial_logger/app_sensors.h"
#include "bsp/bme280/bme280.h"
#include "bsp/bme280/bme280_recovery.h"
#include "bsp/mpu6050/mpu6050.h"
#include "platform/config/config.h"
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

/* The Sensor task is not the console voice any more (the Monitor 5 s block is);
 * failure prints are rate-limited to every 10th attempt (~5 s) so a dead bus
 * cannot flood the log, and per-sample prints are gone entirely. */
#define FAIL_PRINT_EVERY 10u

static int s_bme_inited;
static int s_mpu_inited;
static uint32_t s_bme_fails;
static uint32_t s_mpu_fails;

static bme280_recovery_t s_bme_recov;
static int s_bme_recov_ready;

static void bme_recov_once(void)
{
  if (!s_bme_recov_ready) {
    bme280_recovery_init(&s_bme_recov, BME_RECOVER_AFTER_FAILS);
    s_bme_recov_ready = 1;
  }
}

static void bme_sample(float *t, float *h, float *p)
{
  if (!s_bme_inited) {
    diag_bench_gpio_set(1, true); /* BENCH_GPIO1 high for the whole I2C attempt (scope) */
    int rc = bme280_init(&hi2c1);
    diag_bench_gpio_set(1, false);
    if (rc == 0) {
      bme_recov_once();
      bme280_recovery_on_success(&s_bme_recov);
      s_bme_fails = 0;
      s_bme_inited = 1;
      printf("BME280 init ok\r\n");
    } else {
      /* Init failures can also mean a WEDGED bus (T6: an IWDG reset landing
       * mid-transaction leaves the slave holding SDA low; the read-path
       * recovery below never runs while !s_bme_inited). Arm the streak on
       * failures too so the GPIO bus-clear fires from this path as well. */
      bme_recov_once();
      if (++s_bme_fails % FAIL_PRINT_EVERY == 0u) {
        printf("BME280 init failed x%u (err cnt=%u)\r\n",
               (unsigned)s_bme_fails, (unsigned)stats_get(ERR_BME_TIMEOUT));
      }
      if (bme280_recovery_on_fail(&s_bme_recov)) {
        printf("BME280 bus recover: 9xSCL+STOP (wedged before init)\r\n");
        bme280_bus_recover();
      }
    }
    return;
  }

  diag_bench_gpio_set(1, true);
  int rc = bme280_read(t, h, p);
  diag_bench_gpio_set(1, false);
  if (rc == 0) {
    s_bme_fails = 0;
    bme280_recovery_on_success(&s_bme_recov);
    return;
  }
  if (++s_bme_fails % FAIL_PRINT_EVERY == 0u) {
    printf("BME280 read failed x%u (err cnt=%u)\r\n",
           (unsigned)s_bme_fails, (unsigned)stats_get(ERR_BME_TIMEOUT));
  }
  if (bme280_recovery_on_fail(&s_bme_recov)) {
    printf("BME280 bus recover: 9xSCL+STOP (SDA stuck), re-init next sample\r\n");
    bme280_bus_recover();
    s_bme_inited = 0; /* re-init clears the wedged slave FSM (T6 evidence) */
  }
}

static void mpu_sample(float *ax, float *ay, float *az)
{
  if (!s_mpu_inited) {
    if (mpu6050_init(&hi2c1) == 0) {
      s_mpu_fails = 0;
      s_mpu_inited = 1;
      printf("MPU6050 init ok\r\n");
    } else if (++s_mpu_fails % FAIL_PRINT_EVERY == 0u) {
      printf("MPU6050 init failed x%u (err cnt=%u)\r\n",
             (unsigned)s_mpu_fails, (unsigned)stats_get(ERR_MPU_TIMEOUT));
    }
    return;
  }
  if (mpu6050_read(ax, ay, az) == 0) {
    s_mpu_fails = 0;
    return;
  }
  if (++s_mpu_fails % FAIL_PRINT_EVERY == 0u) {
    printf("MPU6050 read failed x%u (timeout cnt=%u range cnt=%u)\r\n",
           (unsigned)s_mpu_fails, (unsigned)stats_get(ERR_MPU_TIMEOUT),
           (unsigned)stats_get(ERR_SENSOR_RANGE));
  }
}

void app_sensor_task(void *argument)
{
  (void)argument;
  TickType_t last = xTaskGetTickCount();

  for (;;) {
    float t = 0, h = 0, p = 0, ax = 0, ay = 0, az = 0;

    bme_sample(&t, &h, &p);
    mpu_sample(&ax, &ay, &az);

    sensor_sample_t s = {
      .ts_ms = HAL_GetTick(),
      .t = t, .h = h, .p = p,
      .ax = ax, .ay = ay, .az = az,
    };
    if (xQueueSend(sensorQ, &s, 0) != pdTRUE) {
      stats_error(ERR_Q_DROP); /* drop-newest: never block the sampler */
    }
    wd_heartbeat(WD_HB_SENSOR); /* every sample, success or fail */

    vTaskDelayUntil(&last, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
  }
}
