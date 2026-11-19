#ifndef PLATFORM_CONSOLE_H
#define PLATFORM_CONSOLE_H

/* Retargets printf to the ST-LINK VCP (USART2, 115200 8N1).
 * Call console_init() once from main() before any printf. */
void console_init(void);

#endif /* PLATFORM_CONSOLE_H */
