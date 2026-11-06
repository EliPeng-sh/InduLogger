#ifndef APP_INDUSTRIAL_LOGGER_APP_SENSORS_H
#define APP_INDUSTRIAL_LOGGER_APP_SENSORS_H

/* T6 smoke harness: call every 2 s from the default task.
 * Task 9 replaces this with the real Sensor task. */
void app_sensors_tick(void);

#endif /* APP_INDUSTRIAL_LOGGER_APP_SENSORS_H */
