/* USART1 IDLE-line DMA receiver plumbing (design spec §4.2).
 *
 * Circular DMA fills s_ring_buf continuously; the HAL callback (ISR context)
 * derives the DMA write offset from the stream counter and posts ONLY that
 * offset to a queue. No parsing, no logging, no blocking API in the ISR:
 * the CommTask consumer drains via uart_link_read(). */
#include "bsp/uart_link/uart_link.h"
#include "bsp/uart_ring/uart_ring.h"

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "queue.h"

/* CubeMX 6.12 generates these in main.c (no usart.h header pair). */
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;

#define UART_LINK_RING_SIZE 1024u
#define UART_LINK_Q_DEPTH   8u

static uint8_t s_ring_buf[UART_LINK_RING_SIZE];
static uart_ring_t s_ring;
static QueueHandle_t s_wr_q;
static volatile uint32_t s_ev_drop;

void uart_link_init(void)
{
    uring_init(&s_ring, s_ring_buf, UART_LINK_RING_SIZE);
    s_wr_q = xQueueCreate(UART_LINK_Q_DEPTH, sizeof(uint16_t));
    configASSERT(s_wr_q != NULL);
    /* Circular mode: armed once; HAL keeps the DMA running and fires the
     * RxEvent callback on IDLE, half-transfer and full-transfer events. */
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_ring_buf, UART_LINK_RING_SIZE);
}

/* Signature fixed by HAL (weak symbol in stm32f4xx_hal_uart.h): the
 * huart parameter cannot be made const here; cppcheck is suppressed
 * for this file in ci.yml. */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    (void)Size; /* offset comes from the hardware counter, not HAL's Size */
    if (huart->Instance == USART1 && s_wr_q != NULL) {
        /* CNDTR counts down 1024..1 in circular mode -> wr is 0..1023. */
        uint16_t wr = (uint16_t)(UART_LINK_RING_SIZE -
                                 __HAL_DMA_GET_COUNTER(&hdma_usart1_rx));
        BaseType_t higher_prio_woken = pdFALSE;
        if (xQueueSendFromISR(s_wr_q, &wr, &higher_prio_woken) != pdTRUE) {
            s_ev_drop++; /* consumer too slow; bytes still in ring until
                          * the next event carries the same write offset */
        }
        portYIELD_FROM_ISR(higher_prio_woken);
    }
}

size_t uart_link_read(uint8_t *dst, size_t max, bool *overrun)
{
    size_t total = 0;
    bool ov = false;
    uint16_t wr;

    *overrun = false;
    if (s_wr_q == NULL) {
        return 0;
    }
    /* Pop queued offsets in FIFO order; drain each against the ring reader
     * pointer. If max caps a drain, the unread span stays in the ring and
     * the next offset (or a later one) carries it — uring_drain is span-
     * based, so nothing is lost, only coalesced. */
    while (total < max && xQueueReceive(s_wr_q, &wr, 0) == pdTRUE) {
        total += uring_drain(&s_ring, wr, dst + total, max - total, &ov);
    }
    *overrun = ov;
    return total;
}

uint32_t uart_link_ev_drop(void)
{
    return s_ev_drop;
}
