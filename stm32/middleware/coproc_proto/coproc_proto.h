#ifndef COPROC_PROTO_H
#define COPROC_PROTO_H

#include <stddef.h>
#include <stdint.h>

/* ---- Tunables (M2.2 may adjust values; structure is frozen) ---- */
#define CP_SOF0           0xA5u
#define CP_SOF1           0x5Au
#define CP_PAYLOAD_MAX    512u
#define CP_FRAME_MAX      520u  /* 2 SOF + 4 hdr + CP_PAYLOAD_MAX + 2 CRC */

/* ---- CMD codes (v1 set; OTA group reserved for v2) ---- */
#define CP_CMD_PING             0x01u
#define CP_CMD_HEARTBEAT        0x02u
#define CP_CMD_DATA_REPORT      0x10u
#define CP_CMD_DIAG_REPORT      0x11u
#define CP_CMD_MQTT_PUB         0x20u
#define CP_CMD_MQTT_SUB         0x21u
#define CP_CMD_WIFI_CONFIG_SET  0x30u
#define CP_CMD_WIFI_CONFIG_GET  0x31u
#define CP_CMD_NET_STATUS       0x40u
#define CP_CMD_TIME_SET         0x50u  /* ESP32 -> STM32 (direction fixed 2026-11-20) */
#define CP_CMD_ACK              0x60u

/* ---- ACK status codes (ACK payload = [SEQ(1B), STATUS(1B)]) ---- */
#define CP_ACK_OK               0x00u
#define CP_ACK_ERR_UNKNOWN_CMD  0x01u
#define CP_ACK_ERR_BAD_LEN      0x02u
#define CP_ACK_ERR_BAD_ARG      0x03u

typedef enum {
    CP_OK = 0,
    CP_ERR_ARG,
    CP_ERR_BUF,
    CP_ERR_LEN,
    CP_ERR_CRC,
    CP_ERR_NO_FRAME,
    CP_ERR_BUSY
} cp_status_t;

typedef struct {
    uint8_t  seq;
    uint8_t  cmd;
    uint16_t len;                        /* payload bytes, 0..CP_PAYLOAD_MAX */
    uint8_t  payload[CP_PAYLOAD_MAX];
} cp_frame_t;

/* ---- Streaming parser (survives arbitrary byte chunking) ---- */
typedef enum {
    CP_P_SOF0, CP_P_SOF1, CP_P_LEN_H, CP_P_LEN_L,
    CP_P_SEQ,  CP_P_CMD,  CP_P_PAYLOAD,
    CP_P_CRC_H, CP_P_CRC_L
} cp_parse_state_t;

typedef struct {
    cp_parse_state_t state;
    uint8_t  hdr[4];   /* LEN_H, LEN_L, SEQ, CMD; reused for CRC_H at CRC stage */
    uint16_t len;      /* claimed payload length */
    uint16_t got;      /* payload bytes received so far */
    uint8_t  payload[CP_PAYLOAD_MAX];
} cp_parser_t;         /* ~530 B, allocate statically */

/* CRC-16/CCITT-False (poly 0x1021, init 0xFFFF, no reflect, xorout 0). */
uint16_t cp_crc16(const uint8_t *data, size_t len);

/* Encode one frame; returns total bytes (8+len), 0 on error
 * (NULL args, f->len > CP_PAYLOAD_MAX, buf_sz too small). */
size_t cp_encode(const cp_frame_t *f, uint8_t *buf, size_t buf_sz);

void cp_parser_init(cp_parser_t *p);

/* Feed len bytes; returns:
 * CP_OK         - complete frame in *out, *consumed = bytes through CRC_L
 * CP_ERR_NO_FRAME - bytes buffered, frame incomplete, *consumed = len
 * CP_ERR_CRC    - frame dropped, parser reset, *consumed = through CRC_L
 * CP_ERR_LEN    - over-length frame dropped, parser reset,
 *                 *consumed = through LEN_L
 * CP_ERR_ARG    - NULL p/data/consumed
 * out may be NULL (validate-only). Caller re-feeds from data+*consumed. */
cp_status_t cp_parse(cp_parser_t *p, const uint8_t *data, size_t len,
                     cp_frame_t *out, size_t *consumed);

#endif /* COPROC_PROTO_H */
