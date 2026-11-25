/* uart_ring.h — pure-C drain view over a DMA circular RX buffer.
 *
 * The DMA writer owns the write cursor (read from the hardware counter);
 * this module owns only the reader side (rd). HAL-free and ISR-agnostic:
 * callers pass the current write position, we compute the readable span.
 * See docs/specs/2026-11-25-m2-2-uart-link-design.md §4.1.
 */
#ifndef UART_RING_H
#define UART_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t  *buf;   /* backing circular buffer, owned by caller */
    uint16_t  size;  /* capacity, bytes */
    uint16_t  rd;    /* reader cursor (ours); writer cursor is passed per call */
} uart_ring_t;

void   uring_init(uart_ring_t *r, uint8_t *buf, uint16_t size);

/* span = (wr - rd) mod size.
 * span > size/2  => writer lapped reader: rd = wr, *overrun = true, return 0.
 * Else copy min(span, max) bytes (two segments across the wrap), advance rd,
 * *overrun = false, return copied count. */
size_t uring_drain(uart_ring_t *r, uint16_t wr, uint8_t *dst, size_t max,
                   bool *overrun);

#endif /* UART_RING_H */
