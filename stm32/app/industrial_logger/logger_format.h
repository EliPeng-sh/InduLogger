#ifndef APP_LOGGER_FORMAT_H
#define APP_LOGGER_FORMAT_H

#include <stddef.h>
#include <stdint.h>

/* Render one sensor sample as a CSV row for indulog.csv:
 * "ts_ms,temp_c,hum_pct,press_hpa,ax_g,ay_g,az_g\n" (2 decimals).
 * Returns the number of characters written (excluding NUL), or a negative
 * value if the buffer was too small. */
int logger_format_csv(char *buf, size_t len, uint32_t ts_ms,
                      float t, float h, float p, float ax, float ay, float az);

#endif /* APP_LOGGER_FORMAT_H */
