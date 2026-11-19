#ifndef BSP_BME280_RECOVERY_H
#define BSP_BME280_RECOVERY_H

/* Decision logic for the BME280 I2C stuck-bus recovery (host-testable, HAL-free).
 *
 * Background: pulling a wire mid-transaction can wedge the slave I2C FSM, which
 * then holds SDA low; HAL only burns timeouts and never clears the bus (proven
 * on board: replug does NOT recover, only a power cycle does). This module
 * decides WHEN to run the GPIO bus-clear (9x SCL + STOP per AN4235) based on a
 * consecutive-failure streak. The GPIO sequence itself lives in bme280.c
 * (bme280_bus_recover), board-verified by fault re-injection.
 */

typedef struct {
  unsigned streak;    /* consecutive read failures since last success/trigger */
  unsigned threshold; /* trigger bus-clear when streak reaches this */
} bme280_recovery_t;

/* (Re)initialize state with a trigger threshold. */
void bme280_recovery_init(bme280_recovery_t *r, unsigned threshold);

/* A read succeeded: clears the streak. */
void bme280_recovery_on_success(bme280_recovery_t *r);

/* A read failed. Returns 1 exactly on the failure that reaches the threshold
 * (and resets the streak so the next trigger needs a fresh full streak),
 * 0 otherwise. */
int bme280_recovery_on_fail(bme280_recovery_t *r);

#endif /* BSP_BME280_RECOVERY_H */
