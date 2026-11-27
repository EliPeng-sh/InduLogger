/* InduLogger M2.2 CommTask: stop-and-wait session driver for USART1.
 * Design spec §4.3. This task is the ONLY caller of cp_session_send/tick/feed
 * (stop-and-wait serialization point). Priorities: monitor(4) > sensor(3) >
 * comm(2) > logger(1) — comm must never starve the 1 s IWDG feeder. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "stm32f4xx_hal.h"

#include "bsp/uart_link/uart_link.h"
#include "middleware/coproc_proto/coproc_proto.h"
#include "middleware/coproc_proto/coproc_session.h"
#include "platform/comm/app_comm.h"

/* CubeMX 6.12 keeps the UART handles in main.c (no usart.h pair). */
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

#define COMM_PERIOD_MS   50u    /* << 500 ms ACK timeout: honest retransmits */
#define COMM_STAT_MS     10000u
#define COMM_BOOT_TICKS  20u    /* first PING after 20 × 50 ms loops */
#define COMM_TXQ_DEPTH   4u
#define COMM_TX_TIMEOUT  100u   /* ms; 520 B @115200 ~ 45 ms worst case */
#define COMM_TASK_STACK  1024u  /* words; session/frame state is static */

typedef struct {
    uint8_t  cmd;
    uint16_t len;
    uint8_t  pl[CP_PAYLOAD_MAX];
} comm_txq_item_t;

/* All protocol state static, never on the task stack (spec §4.3). */
static cp_session_t s_sess;
static cp_frame_t   s_in;
static uint8_t      s_rx[CP_FRAME_MAX];
static uint8_t      s_ack[10];
static uint8_t      s_txbuf[CP_FRAME_MAX];
static QueueHandle_t s_txq;
static volatile uint32_t s_time_unix;
static volatile bool     s_time_valid;
static uint32_t       s_orr;    /* ring overruns observed by this task */

void app_comm_task(void *arg)
{
    (void)arg;
    s_txq = xQueueCreate(COMM_TXQ_DEPTH, sizeof(comm_txq_item_t));
    configASSERT(s_txq != NULL);

    TickType_t last_wake = xTaskGetTickCount();
    const uint32_t t0 = HAL_GetTick();
    uint32_t boot_wait = COMM_BOOT_TICKS; /* first PING ~1 s after boot */
    uint32_t next_stat = t0 + COMM_STAT_MS;
    cp_link_state_t last_link;

    cp_session_init(&s_sess, t0);
    last_link = s_sess.link;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(COMM_PERIOD_MS));
        const uint32_t now = HAL_GetTick();

        /* 1. RX: drain ring -> streaming parser (one frame per feed call). */
        bool overrun = false;
        size_t n = uart_link_read(s_rx, sizeof(s_rx), &overrun);
        if (overrun) {
            s_orr++;
        }
        size_t off = 0;
        while (off < n) {
            size_t consumed = 0;
            size_t ack_len = 0;
            memset(&s_in, 0, sizeof(s_in));
            cp_status_t st = cp_session_feed(&s_sess, s_rx + off, n - off,
                                             now, &s_in, &consumed,
                                             s_ack, sizeof(s_ack), &ack_len);
            if (st == CP_ERR_NO_FRAME || consumed == 0) {
                break;
            }
            off += consumed;
            if (ack_len > 0) {
                HAL_UART_Transmit(&huart1, s_ack, (uint16_t)ack_len,
                                  COMM_TX_TIMEOUT);
            }
            if (s_in.cmd == CP_CMD_PING) {
                /* R9: STM32 never re-echoes; log only. */
                printf("[comm] PING echo seq=%u len=%u\r\n",
                       (unsigned)s_in.seq, (unsigned)s_in.len);
            } else if (s_in.cmd == CP_CMD_TIME_SET && s_in.len == 4u) {
                s_time_unix = ((uint32_t)s_in.payload[0] << 24) |
                              ((uint32_t)s_in.payload[1] << 16) |
                              ((uint32_t)s_in.payload[2] << 8) |
                              ((uint32_t)s_in.payload[3]);
                s_time_valid = true;
                printf("[comm] TIME_SET unix=%lu\r\n",
                       (unsigned long)s_time_unix);
            } else if (s_in.cmd != 0u) {
                printf("[comm] RX cmd=0x%02X len=%u\r\n",
                       (unsigned)s_in.cmd, (unsigned)s_in.len);
            }
        }

        /* 2. TX queue: the only cp_session_send call site. */
        comm_txq_item_t item;
        if (xQueueReceive(s_txq, &item, 0) == pdTRUE) {
            size_t out_len = 0;
            if (cp_session_send(&s_sess, item.cmd, item.pl, item.len,
                                now, s_txbuf, sizeof(s_txbuf),
                                &out_len) == CP_OK) {
                HAL_UART_Transmit(&huart1, s_txbuf, (uint16_t)out_len,
                                  COMM_TX_TIMEOUT);
            }
        }

        /* 3. Session tick: heartbeat + retransmit + link supervision.
         * Wrap-safe: HAL_GetTick is a 32-bit ms counter (pitfall 20) —
         * all deadline compares here are library-internal (subtraction
         * based); app-level timing uses only vTaskDelayUntil periods. */
        size_t tick_len = 0;
        if (cp_session_tick(&s_sess, now, s_txbuf, sizeof(s_txbuf),
                            &tick_len) == CP_OK) {
            HAL_UART_Transmit(&huart1, s_txbuf, (uint16_t)tick_len,
                              COMM_TX_TIMEOUT);
        }

        /* 4. Console trigger: 'p' on ST-LINK VCP sends one demo PING.
         * NOTE: HAL_UART_Receive returns HAL_OK on success — the plan's
         * '== 1' was wrong (1 == HAL_ERROR), caught by board test. */
        uint8_t c;
        if (HAL_UART_Receive(&huart2, &c, 1, 0) == HAL_OK && c == 'p') {
            (void)app_comm_send(CP_CMD_PING, (const uint8_t *)"abc", 3);
        }

        /* 5. Boot PING: counted in RTOS loop iterations (20 × 50 ms), not
         * via HAL_GetTick — immune to any ms-clock fast-forward storm. */
        if (boot_wait > 0u) {
            boot_wait--;
            if (boot_wait == 0u) {
                (void)app_comm_send(CP_CMD_PING, (const uint8_t *)"abc", 3);
            }
        }

        /* 6. Link edge + 10 s counter line. */
        if (s_sess.link != last_link) {
            last_link = s_sess.link;
            printf("[comm] link %s\r\n",
                   (last_link == CP_LINK_UP) ? "UP" : "DOWN");
        }
        if ((int32_t)(now - next_stat) >= 0) {
            next_stat = now + COMM_STAT_MS;
            printf("[comm] tx=%lu acked=%lu retry=%lu fault=%lu rx=%lu "
                   "dup=%lu crc=%lu orr=%lu evd=%lu rmm=%lu\r\n",
                   (unsigned long)s_sess.tx_frames,
                   (unsigned long)s_sess.tx_acked,
                   (unsigned long)s_sess.tx_retries,
                   (unsigned long)s_sess.tx_faults,
                   (unsigned long)s_sess.rx_frames,
                   (unsigned long)s_sess.rx_dup,
                   (unsigned long)s_sess.rx_crc_err,
                   (unsigned long)s_orr,
                   (unsigned long)uart_link_ev_drop(),
                   (unsigned long)uart_link_rearm_count());
        }
    }
}

cp_status_t app_comm_send(uint8_t cmd, const uint8_t *payload, size_t len)
{
    if (len > CP_PAYLOAD_MAX) {
        return CP_ERR_LEN;
    }
    comm_txq_item_t item = { .cmd = cmd, .len = (uint16_t)len };
    if (len > 0 && payload != NULL) {
        memcpy(item.pl, payload, len);
    }
    if (s_txq == NULL || xQueueSend(s_txq, &item, 0) != pdTRUE) {
        return CP_ERR_BUF; /* queue full: visible, never silently dropped */
    }
    return CP_OK;
}

uint32_t app_comm_time(void)
{
    return (uint32_t)s_time_unix;
}

bool app_comm_time_valid(void)
{
    return (bool)s_time_valid;
}
