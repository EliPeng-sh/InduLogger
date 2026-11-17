#ifndef APP_INDUSTRIAL_LOGGER_APP_SENSORS_H
#define APP_INDUSTRIAL_LOGGER_APP_SENSORS_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

/* One fused sample handed from the Sensor task to the Logger task (Task 9).
 * Fields hold the last reading of each sensor; a sensor that failed its
 * init/read simply leaves stale/zero values — the CSV row is raw evidence,
 * not filtered data. */
typedef struct {
  uint32_t ts_ms;
  float t, h, p;    /* BME280 */
  float ax, ay, az; /* MPU6050/6500 */
} sensor_sample_t;

/* Sensor -> Logger queue (created in main.c before the scheduler starts). */
extern QueueHandle_t sensorQ;

/* Sensor task body (Task 9): lazy-inits both I2C sensors (T6 recovery
 * machinery unchanged), samples at SAMPLE_PERIOD_MS, pushes drop-newest into
 * sensorQ, beats the alive-mask watchdog once per sample. */
void app_sensor_task(void *argument);

#endif /* APP_INDUSTRIAL_LOGGER_APP_SENSORS_H */
