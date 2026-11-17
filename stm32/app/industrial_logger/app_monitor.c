#include "app/industrial_logger/app_monitor.h"
#include "platform/stats/stats.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

/* 1 Hz alive-mask feeding. Boot subtlety: the IWDG is already counting when
 * the scheduler starts, and the guarded tasks need their first sample/loop to
 * beat — so the Monitor re-syncs its snapshot once at entry (wd_monitor_reset)
 * to guarantee the first check passes; after that only real progress feeds. */
void app_monitor_task(void *argument)
{
  (void)argument;
  TickType_t last = xTaskGetTickCount();
  uint32_t n = 0;

  wd_monitor_reset();

  for (;;) {
    if (wd_all_alive()) {
      iwdg_feed();
    }

    if (++n % 5u == 0u) { /* 5 s diagnostics block: the console heartbeat */
      UBaseType_t wm_sensor =
          uxTaskGetStackHighWaterMark(sensorTaskHandle);
      UBaseType_t wm_logger =
          uxTaskGetStackHighWaterMark(loggerTaskHandle);
      UBaseType_t wm_monitor =
          uxTaskGetStackHighWaterMark(monitorTaskHandle);
      size_t heap_now = xPortGetFreeHeapSize();
      size_t heap_min = xPortGetMinimumEverFreeHeapSize();

      printf("[mon] up=%us cause=%s wm=%u/%u/%u heap=%u/%u "
             "bme=%u mpu=%u sd=%u qdrop=%u range=%u recov=%u\r\n",
             (unsigned)(xTaskGetTickCount() / configTICK_RATE_HZ),
             reset_cause_name(stats_boot_cause()),
             (unsigned)wm_sensor, (unsigned)wm_logger, (unsigned)wm_monitor,
             (unsigned)heap_now, (unsigned)heap_min,
             (unsigned)stats_get(ERR_BME_TIMEOUT),
             (unsigned)stats_get(ERR_MPU_TIMEOUT),
             (unsigned)stats_get(ERR_SD_WRITE),
             (unsigned)stats_get(ERR_Q_DROP),
             (unsigned)stats_get(ERR_SENSOR_RANGE),
             (unsigned)stats_get(ERR_I2C_RECOVERY));
    }

    vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
  }
}
