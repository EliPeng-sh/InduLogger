#ifndef PLATFORM_STATS_H
#define PLATFORM_STATS_H

#include <stdint.h>

typedef enum {
  RESET_CAUSE_UNKNOWN = 0,
  RESET_CAUSE_BOR,
  RESET_CAUSE_PIN,
  RESET_CAUSE_POR,
  RESET_CAUSE_SOFT,
  RESET_CAUSE_IWDG,
  RESET_CAUSE_WWDG,
  RESET_CAUSE_LPWR
} reset_cause_t;

/* Pure decode of the reset-cause enum to a stable tag string. Host-tested. */
const char *reset_cause_name(reset_cause_t c);

/* Error counters (evidence infra, spec §3): every failure path calls
 * stats_error(ERR_x); the Monitor task and Modbus diag registers expose them. */
void stats_error(uint32_t id);
uint32_t stats_get(uint32_t id);

extern const uint32_t ERR_BME_TIMEOUT;
extern const uint32_t ERR_MPU_TIMEOUT;
extern const uint32_t ERR_SD_WRITE;
extern const uint32_t ERR_Q_DROP;
extern const uint32_t ERR_SENSOR_RANGE;
extern const uint32_t ERR_I2C_RECOVERY;

/* Target-side (stm32f4xx-only, defined in stats.c under #ifndef HOST_TEST):
 * declarations are unguarded so main.c can call them; the host build never links them. */
reset_cause_t reset_cause_capture(void);
void iwdg_feed(void);

#endif /* PLATFORM_STATS_H */
