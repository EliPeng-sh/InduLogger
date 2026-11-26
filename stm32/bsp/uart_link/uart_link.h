/* uart_link.h — USART1 IDLE-line + circular-DMA receiver plumbing.
 *
 * DMA writes into a 1024 B ring; the HAL RxEvent callback (ISR context)
 * only computes the DMA write offset and posts it to a small queue.
 * Consumers (CommTask) drain via uart_link_read(). No parsing, no
 * blocking APIs in ISR — see design spec §4.2.
 */
#ifndef UART_LINK_H
#define UART_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void     uart_link_init(void);   /* create queue + arm ReceiveToIdle_DMA once */
size_t   uart_link_read(uint8_t *dst, size_t max, bool *overrun);
uint32_t uart_link_ev_drop(void); /* events lost to a full queue (ISR side) */
uint32_t uart_link_rearm_count(void); /* RX DMA re-arms after HAL abort-on-error */

#endif /* UART_LINK_H */
