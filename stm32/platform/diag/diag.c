#include "platform/diag/diag.h"

void diag_dwt_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  /* Bench pins as push-pull outputs, driven low (baseline for the scope).
   * Label mapping per InduLogger.ioc: BENCH_GPIO1 = PB0, BENCH_GPIO2 = PB1. */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitTypeDef g = { 0 };
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Pin = GPIO_PIN_0 | GPIO_PIN_1;
  HAL_GPIO_Init(GPIOB, &g);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_1, GPIO_PIN_RESET);
}

void diag_pulse(void)
{
  DIAG_PULSE(GPIOA, GPIO_PIN_1);
}

void diag_bench_gpio_set(int n, bool on)
{
  if (n == 1) {
    /* BENCH_GPIO1 = PB0 (Arduino A3) — label mapping per InduLogger.ioc. */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
  } else if (n == 2) {
    /* BENCH_GPIO2 = PB1 (Arduino A4). */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
  }
}
