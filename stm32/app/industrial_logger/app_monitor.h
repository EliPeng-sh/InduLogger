#ifndef APP_INDUSTRIAL_LOGGER_APP_MONITOR_H
#define APP_INDUSTRIAL_LOGGER_APP_MONITOR_H

#include "FreeRTOS.h"
#include "task.h"

/* Created in main.c (USER CODE) before the scheduler starts; the Monitor task
 * reads the handles for per-task stack high-water marks. */
extern TaskHandle_t sensorTaskHandle;
extern TaskHandle_t loggerTaskHandle;
extern TaskHandle_t monitorTaskHandle;

/* Monitor task body (Task 9): 1 Hz alive-mask check feeds the IWDG only when
 * every guarded task has beaten since the previous check; every 5 s it prints
 * the diagnostics block (uptime, reset cause, per-task wm, heap, err counters)
 * — the console heartbeat for the whole system. */
void app_monitor_task(void *argument);

#endif /* APP_INDUSTRIAL_LOGGER_APP_MONITOR_H */
