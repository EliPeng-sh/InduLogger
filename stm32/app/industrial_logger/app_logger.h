#ifndef APP_INDUSTRIAL_LOGGER_APP_LOGGER_H
#define APP_INDUSTRIAL_LOGGER_APP_LOGGER_H

#include "fatfs.h"

/* SD logging (Task 8): boot-time mount + append + readback evidence, then a
 * periodic tick that appends and self-heals around card removal. T9's logger
 * task reuses sdcard_mount()/sdcard_append_line() through this module. */

/* Mount, append the boot evidence line, read the first line back and print it
 * ("sd: mounted, readback: boot,ok"). Safe to call with no card: bounded
 * failures print and return. */
void app_logger_boot(void);

/* Logger task body (Task 9): boot-once + CSV header-once, then drain sensorQ
 * into LOG_PATH rows; re-mounts when the card comes back and beats the
 * alive-mask watchdog on every loop iteration (incl. queue-timeout paths). */
void app_logger_task(void *argument);

FRESULT sdcard_mount(void);
FRESULT sdcard_append_line(const char *path, const char *line);

#endif /* APP_INDUSTRIAL_LOGGER_APP_LOGGER_H */
