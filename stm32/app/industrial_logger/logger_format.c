#include "app/industrial_logger/logger_format.h"

#include <inttypes.h>
#include <stdio.h>

/* Row layout (contract pinned by host/tests/test_csv_format.c):
 * "ts_ms,temp_c,hum_pct,press_hpa,ax_g,ay_g,az_g\n" — 2 decimals each.
 * Target-side float formatting needs -u _printf_float (already in the
 * firmware LDFLAGS since Task 1). */
int logger_format_csv(char *buf, size_t len, uint32_t ts_ms,
                      float t, float h, float p, float ax, float ay, float az)
{
  int n = snprintf(buf, len,
                   "%" PRIu32 ",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                   ts_ms, t, h, p, ax, ay, az);
  if (n < 0 || (size_t)n >= len) {
    return -1; /* encoding error or buffer too small (truncated) */
  }
  return n;
}
