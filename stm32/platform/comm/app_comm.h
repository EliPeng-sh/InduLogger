#ifndef APP_COMM_H
#define APP_COMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "middleware/coproc_proto/coproc_proto.h" /* cp_status_t */

/* FreeRTOS task function for xTaskCreate (prio 2, stack 1024 words).
 * Owns the single cp_session_t: the ONLY caller of cp_session_send/tick/feed. */
void app_comm_task(void *arg);

/* Queue one command frame for transmission by CommTask (serialization
 * point for the stop-and-wait protocol). Returns CP_ERR_BUF if the
 * outbound queue (depth 4) is full; payload longer than CP_PAYLOAD_MAX
 * returns CP_ERR_LEN. Never touches the UART itself. */
cp_status_t app_comm_send(uint8_t cmd, const uint8_t *payload, size_t len);

/* TIME_SET landing (RAM only until RTC, spec §4.3): unix seconds from
 * the last 4-byte big-endian payload; valid=false until first frame. */
uint32_t app_comm_time(void);
bool     app_comm_time_valid(void);

#endif /* APP_COMM_H */
