/* InduLogger ESP32-S3 M2.2 link task: UART1 slave of the coproc protocol.
 * Echo responder for PING (R9: only this side echoes), drives the session
 * tick, logs link edges and periodic counters. */
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "coproc_proto.h"
#include "coproc_session.h"
#include "link_task.h"

#define LINK_UART        UART_NUM_1
/* Hole-11 marginal contact (rounds 4-8: route lives ~1-2 min after each
 * reseat then dies; wire bodies x3, PA10 hole, GPIO11 pad all proven good).
 * Test/adopt: TX moved to GPIO13. */
#define LINK_UART_TX_GPIO GPIO_NUM_13
#define LINK_UART_RX_GPIO GPIO_NUM_12
#define LINK_RX_Q_SIZE   256          /* bytes buffered per pull */
#define LINK_TICK_MS     50u           /* session tick period */
#define LINK_STAT_MS     10000u        /* counter line period */
#define LINK_DEMO_MS     2000u         /* loopback demo PING period */
#define LINK_TASK_STACK  4096

static const char *TAG = "comm";

/* Static: cp_session_t is 1.1 KB, cp_frame_t 516 B - never on the stack. */
static cp_session_t s_sess;
static uint8_t s_rxbuf[LINK_RX_Q_SIZE];
static uint8_t s_txbuf[CP_FRAME_MAX];
static uint8_t s_ack[10];

static void link_tx(const uint8_t *bytes, size_t len)
{
    if (len > 0) {
        uart_write_bytes(LINK_UART, (const char *)bytes, len);
    }
}

static uint32_t link_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void handle_delivered_frame(cp_frame_t *in, uint32_t now)
{
    if (in->cmd == CP_CMD_PING) {
        ESP_LOGI(TAG, "PING seq=%u len=%u", (unsigned)in->seq, (unsigned)in->len);
        /* R9: echo the payload straight back as a PING. */
        size_t out_len = 0;
        cp_status_t st = cp_session_send(&s_sess, CP_CMD_PING, in->payload,
                                         in->len, now, s_txbuf,
                                         sizeof(s_txbuf), &out_len);
        if (st == CP_OK) {
            link_tx(s_txbuf, out_len);
        } else if (st != CP_ERR_BUSY) {
            /* CP_ERR_BUSY is legitimate: our own stop-and-wait frame is
             * still pending; the auto-ACK already answers the peer. */
            ESP_LOGW(TAG, "echo send refused st=%d", (int)st);
        }
    } else if (in->cmd != 0) {
        ESP_LOGI(TAG, "RX cmd=0x%02X len=%u", (unsigned)in->cmd, (unsigned)in->len);
    }
}

static void drain_rx(uint32_t now)
{
    int n = uart_read_bytes(LINK_UART, s_rxbuf, sizeof(s_rxbuf), 0);
    size_t off = 0;
    while (n > 0 && off < (size_t)n) {
        cp_frame_t in;
        size_t consumed = 0;
        size_t ack_len = 0;
        memset(&in, 0, sizeof(in));
        cp_session_feed(&s_sess, s_rxbuf + off, (size_t)n - off, now, &in,
                        &consumed, s_ack, sizeof(s_ack), &ack_len);
        if (consumed == 0) {
            break; /* parser needs more bytes; wait for next pull */
        }
        off += consumed;
        if (ack_len > 0) {
#if CONFIG_COPROC_SUPPRESS_ACK
            ESP_LOGD(TAG, "ACK suppressed (%u B)", (unsigned)ack_len);
#else
            link_tx(s_ack, ack_len);
#endif
        }
        handle_delivered_frame(&in, now);
    }
}

static void link_task_body(void *arg)
{
    (void)arg;
    uart_config_t uart_cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,   /* enum starts at 5-bits=0: must not omit! */
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(LINK_UART, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(LINK_UART, LINK_UART_TX_GPIO,
                                 LINK_UART_RX_GPIO, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART, 1024, 0, 0, NULL, 0));

    uint32_t now = link_now_ms();
    cp_session_init(&s_sess, now);

    uint32_t next_tick = now + LINK_TICK_MS;
    uint32_t next_stat = now + LINK_STAT_MS;
#if CONFIG_COPROC_LOOPBACK_DEMO
    uint32_t next_demo = now + LINK_DEMO_MS;
#endif
    cp_link_state_t last_link = s_sess.link;

    ESP_LOGI(TAG, "link task up: UART1 tx=%d rx=%d @115200",
             (int)LINK_UART_TX_GPIO, (int)LINK_UART_RX_GPIO);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        now = link_now_ms();

        drain_rx(now);

        /* Wrap-safe deadline compare (AGENTS pitfall 20): signed diff,
         * never 'now >= absolute_deadline' on ms counters. */
        if ((int32_t)(now - next_tick) >= 0) {
            next_tick = now + LINK_TICK_MS;
            size_t out_len = 0;
            if (cp_session_tick(&s_sess, now, s_txbuf, sizeof(s_txbuf),
                                &out_len) == CP_OK) {
                link_tx(s_txbuf, out_len);
            }
        }

#if CONFIG_COPROC_LOOPBACK_DEMO
        if ((int32_t)(now - next_demo) >= 0) {
            next_demo = now + LINK_DEMO_MS;
            size_t out_len = 0;
            if (cp_session_send(&s_sess, CP_CMD_PING,
                                (const uint8_t *)"abc", 3, now, s_txbuf,
                                sizeof(s_txbuf), &out_len) == CP_OK) {
                link_tx(s_txbuf, out_len);
                /* tx_seq only advances on ACK; the seq just used is in pending. */
                ESP_LOGI(TAG, "demo PING tx seq=%u", (unsigned)s_sess.pending.seq);
            }
        }
#endif

        if (s_sess.link != last_link) {
            last_link = s_sess.link;
            ESP_LOGI(TAG, "link %s", (last_link == CP_LINK_UP) ? "UP" : "DOWN");
        }
        if ((int32_t)(now - next_stat) >= 0) {
            next_stat = now + LINK_STAT_MS;
            ESP_LOGI(TAG, "tx=%" PRIu32 " acked=%" PRIu32 " retry=%" PRIu32
                     " fault=%" PRIu32 " rx=%" PRIu32 " crc=%" PRIu32,
                     s_sess.tx_frames, s_sess.tx_acked, s_sess.tx_retries,
                     s_sess.tx_faults, s_sess.rx_frames, s_sess.rx_crc_err);
        }
    }
}

void link_task_start(void)
{
    BaseType_t ok = xTaskCreate(link_task_body, "coproc_link",
                                LINK_TASK_STACK, NULL, 5, NULL);
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
