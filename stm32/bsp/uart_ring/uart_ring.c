/* uart_ring.c — reader side of a DMA circular RX buffer (spec §4.1).
 * Pure C: no HAL, no ISR assumptions; uint16 modular arithmetic. */
#include "uart_ring.h"

#include <string.h>

void uring_init(uart_ring_t *r, uint8_t *buf, uint16_t size)
{
    r->buf  = buf;
    r->size = size;
    r->rd   = 0u;
}

size_t uring_drain(uart_ring_t *r, uint16_t wr, uint8_t *dst, size_t max,
                   bool *overrun)
{
    uint16_t span;

    /* (wr - rd) mod size, written without relying on size being a
     * power of two (wr, rd are always in [0, size)). */
    if (wr >= r->rd) {
        span = (uint16_t)(wr - r->rd);
    } else {
        span = (uint16_t)(r->size - r->rd + wr);
    }

    if (span > (uint16_t)(r->size / 2u)) {
        /* Writer lapped us: data in between is garbage; resync. */
        r->rd    = wr;
        *overrun = true;
        return 0u;
    }
    *overrun = false;

    size_t n = (max < (size_t)span) ? max : (size_t)span;
    if (n == 0u) {
        return 0u;
    }

    uint16_t first = (uint16_t)(r->size - r->rd); /* contiguous to end */
    if (n <= first) {
        memcpy(dst, &r->buf[r->rd], n);
    } else {
        memcpy(dst, &r->buf[r->rd], first);
        memcpy(dst + first, &r->buf[0], n - first);
    }
    r->rd = (uint16_t)((r->rd + n) % r->size);
    return n;
}
