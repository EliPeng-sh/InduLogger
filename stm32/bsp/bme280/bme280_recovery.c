#include "bsp/bme280/bme280_recovery.h"

void bme280_recovery_init(bme280_recovery_t *r, unsigned threshold)
{
  r->streak = 0u;
  r->threshold = threshold;
}

void bme280_recovery_on_success(bme280_recovery_t *r)
{
  r->streak = 0u;
}

int bme280_recovery_on_fail(bme280_recovery_t *r)
{
  r->streak++;
  if (r->streak >= r->threshold) {
    r->streak = 0u; /* re-arm: next trigger needs a fresh full streak */
    return 1;
  }
  return 0;
}
