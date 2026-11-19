#include "stats.h"

const uint32_t ERR_BME_TIMEOUT  = 0;
const uint32_t ERR_MPU_TIMEOUT  = 1;
const uint32_t ERR_SD_WRITE     = 2;
const uint32_t ERR_Q_DROP       = 3;
const uint32_t ERR_SENSOR_RANGE = 4;
const uint32_t ERR_I2C_RECOVERY = 5;

#define STATS_ERR_COUNT 8
static volatile uint32_t s_err_counts[STATS_ERR_COUNT];

void stats_error(uint32_t id) {
  if (id < STATS_ERR_COUNT) {
    (void)__atomic_fetch_add(&s_err_counts[id], 1U, __ATOMIC_RELAXED);
  }
}

uint32_t stats_get(uint32_t id) {
  return (id < STATS_ERR_COUNT) ? __atomic_load_n(&s_err_counts[id], __ATOMIC_RELAXED) : 0U;
}

const char *reset_cause_name(reset_cause_t c) {
  switch (c) {
  case RESET_CAUSE_BOR:  return "brownout";
  case RESET_CAUSE_PIN:  return "external-pin";
  case RESET_CAUSE_POR:  return "power-on";
  case RESET_CAUSE_SOFT: return "software";
  case RESET_CAUSE_IWDG: return "iwdg";
  case RESET_CAUSE_WWDG: return "wwdg";
  case RESET_CAUSE_LPWR: return "low-power";
  default:               return "unknown";
  }
}

#ifndef HOST_TEST

#include "stm32f4xx_hal.h"

/* Defined by CubeMX in Core/Src/main.c */
extern IWDG_HandleTypeDef hiwdg;

void iwdg_feed(void) {
  HAL_IWDG_Refresh(&hiwdg);
}

/* Reads RCC->CSR reset flags (RM0383) and clears them. Distinctive flags are
 * checked before PINRSTF — the pin flag also sets on POR and IWDG cycles. */
reset_cause_t reset_cause_capture(void) {
  const uint32_t csr = RCC->CSR;
  reset_cause_t cause = RESET_CAUSE_UNKNOWN;

  if (csr & RCC_CSR_IWDGRSTF)      { cause = RESET_CAUSE_IWDG; }
  else if (csr & RCC_CSR_WWDGRSTF) { cause = RESET_CAUSE_WWDG; }
  else if (csr & RCC_CSR_SFTRSTF)  { cause = RESET_CAUSE_SOFT; }
  else if (csr & RCC_CSR_PORRSTF)  { cause = RESET_CAUSE_POR; }
  else if (csr & RCC_CSR_BORRSTF)  { cause = RESET_CAUSE_BOR; }
  else if (csr & RCC_CSR_PINRSTF)  { cause = RESET_CAUSE_PIN; }
  else if (csr & RCC_CSR_LPWRRSTF) { cause = RESET_CAUSE_LPWR; }

  __HAL_RCC_CLEAR_RESET_FLAGS();
  return cause;
}

#endif /* !HOST_TEST */

/* ---- Alive-mask watchdog (Task 9) ----
 * __atomic builtins compile on both host gcc and arm-none-eabi-gcc, so this
 * logic is host-tested (test_wd_alive) instead of HOST_TEST-guarded. */

static uint32_t s_hb[WD_HB_COUNT];      /* per-task heartbeat counters */
static uint32_t s_hb_snap[WD_HB_COUNT]; /* Monitor's last-seen snapshot */

void wd_heartbeat(uint32_t id)
{
  if (id < WD_HB_COUNT) {
    (void)__atomic_fetch_add(&s_hb[id], 1U, __ATOMIC_RELAXED);
  }
}

bool wd_all_alive(void)
{
  int alive = 1;
  for (uint32_t i = 0; i < WD_HB_COUNT; i++) {
    uint32_t now = __atomic_load_n(&s_hb[i], __ATOMIC_RELAXED);
    if (now == s_hb_snap[i]) {
      alive = 0; /* this task's heartbeat froze since the previous check */
    }
    s_hb_snap[i] = now;
  }
  return alive;
}

void wd_monitor_reset(void)
{
  for (uint32_t i = 0; i < WD_HB_COUNT; i++) {
    s_hb_snap[i] = __atomic_load_n(&s_hb[i], __ATOMIC_RELAXED);
  }
}

/* Boot reset cause cache: capture() clears the RCC flags, so the value must
 * be taken once (main.c boot print takes it first, Monitor reuses it). */
reset_cause_t stats_boot_cause(void)
{
#ifndef HOST_TEST
  static reset_cause_t s_boot_cause = RESET_CAUSE_UNKNOWN;
  static int s_boot_cause_valid;
  if (!s_boot_cause_valid) {
    s_boot_cause = reset_cause_capture();
    s_boot_cause_valid = 1;
  }
  return s_boot_cause;
#else
  return RESET_CAUSE_UNKNOWN; /* host: no RCC flags; cause itself not under test */
#endif
}
