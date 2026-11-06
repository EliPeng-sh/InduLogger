#ifndef PLATFORM_DIAG_H
#define PLATFORM_DIAG_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx_hal.h"

/* DWT cycle counter for task-context timing. At 100 MHz: 100 cycles = 1 us. */
void diag_dwt_init(void);

static inline uint32_t diag_cycles(void)
{
  return DWT->CYCCNT;
}

/* 8-cycle high marker, scope trigger. Same task context as the measured work. */
#define DIAG_PULSE(gpio, pin) do { \
    (gpio)->BSRR = (uint32_t)(pin); \
    for (volatile uint32_t _pulse_i = 0u; _pulse_i < 3u; ++_pulse_i) { } \
    (gpio)->BSRR = ((uint32_t)(pin) << 16); \
  } while (0)

/* BENCH_GPIO1 = PA1 (Arduino A1), BENCH_GPIO2 = PB0 (Arduino A3). */
void diag_pulse(void);
void diag_bench_gpio_set(int n, bool on);

#endif /* PLATFORM_DIAG_H */
