#include "console.h"

#include <stdio.h>
#include <unistd.h>

#include "stm32f4xx_hal.h"

/* This CubeMX generation has no usart.h (no pair files) — huart2 lives in main.c */
extern UART_HandleTypeDef huart2;

void console_init(void) {
  /* USART2 already configured by CubeMX. Unbuffered stdout: no newlib internal
   * 1KB malloc on first printf, output stays immediate. */
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("\r\nInduLogger boot v0.1.0-m1\r\n");
}

int _write(int fd, char *ptr, int len) {
  (void)fd;
  HAL_UART_Transmit(&huart2, (uint8_t *)ptr, (uint16_t)len, 100);
  return len;
}
