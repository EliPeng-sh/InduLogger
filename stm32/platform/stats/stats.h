#ifndef PLATFORM_STATS_H
#define PLATFORM_STATS_H

#include <stdbool.h>
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

/* Alive-mask watchdog (Task 9). Guarded tasks call wd_heartbeat(id) on every
 * loop iteration (including queue-timeout paths); the Monitor calls
 * wd_all_alive() at 1 Hz and feeds the IWDG only when every heartbeat has
 * advanced since the previous check. A hung task freezes its heartbeat, so
 * feeding stops and the IWDG resets within ~2 s — the single-task feed in
 * the default task cannot catch an app-task hang. Implemented with __atomic
 * builtins (compile on host too, so the decision logic is host-tested —
 * plan's HOST_TEST guard turned out unnecessary). */
#define WD_HB_SENSOR 0u
#define WD_HB_LOGGER 1u
#define WD_HB_COUNT  2u

void wd_heartbeat(uint32_t id);
bool wd_all_alive(void);

/* Test hook: re-sync the monitor snapshot to the current counters. The real
 * Monitor never needs it (snapshots start at 0 and tasks beat every loop);
 * host tests use it in setUp for case independence. */
void wd_monitor_reset(void);

/* Boot-time reset cause, captured once and cached: reset_cause_capture()
 * clears the RCC flags (so the next reset decodes cleanly), so callers that
 * print the cause more than once (Monitor diagnostics) must use the cache. */
reset_cause_t stats_boot_cause(void);

#endif /* PLATFORM_STATS_H */
