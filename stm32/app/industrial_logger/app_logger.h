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

/* Periodic append ("tick,<seq>") with auto re-mount when the card disappears
 * or comes back. Every failed append counts into ERR_SD_WRITE. */
void app_logger_tick(void);

FRESULT sdcard_mount(void);
FRESULT sdcard_append_line(const char *path, const char *line);

#endif /* APP_INDUSTRIAL_LOGGER_APP_LOGGER_H */
