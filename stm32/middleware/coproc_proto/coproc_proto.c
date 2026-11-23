#include "coproc_proto.h"

/* cp_encode / cp_parse remain RED stubs until Tasks 3-4. */

/* CRC-16/CCITT-False: poly 0x1021, init 0xFFFF, no reflection, xorout 0.
 * check("123456789") == 0x29B1 (independent Python oracle verified).
 * crc_update chains a running CRC so callers can cover a frame assembled
 * from disjoint buffers (parser reuses it for header + payload). */
static uint16_t crc_update(uint16_t crc, const uint8_t *data, size_t len)
{
    if (data == NULL) {
        return crc;
    }
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t cp_crc16(const uint8_t *data, size_t len)
{
    return crc_update(0xFFFFu, data, len);
}

size_t cp_encode(const cp_frame_t *f, uint8_t *buf, size_t buf_sz)
{
    if (f == NULL || buf == NULL || f->len > CP_PAYLOAD_MAX) {
        return 0u;
    }
    const size_t total = 6u + (size_t)f->len + 2u;
    if (buf_sz < total) {
        return 0u;
    }
    buf[0] = CP_SOF0;
    buf[1] = CP_SOF1;
    buf[2] = (uint8_t)(f->len >> 8);
    buf[3] = (uint8_t)(f->len & 0xFFu);
    buf[4] = f->seq;
    buf[5] = f->cmd;
    for (uint16_t i = 0; i < f->len; i++) {
        buf[6u + i] = f->payload[i];
    }
    uint16_t crc = cp_crc16(&buf[2], 4u + (size_t)f->len);
    buf[6u + f->len] = (uint8_t)(crc >> 8);
    buf[7u + f->len] = (uint8_t)(crc & 0xFFu);
    return total;
}

void cp_parser_init(cp_parser_t *p)
{
    (void)p;
}

cp_status_t cp_parse(cp_parser_t *p, const uint8_t *data, size_t len,
                     cp_frame_t *out, size_t *consumed)
{
    (void)p;
    (void)data;
    (void)len;
    (void)out;
    if (consumed != NULL) {
        *consumed = 0u;
    }
    return CP_ERR_ARG;
}
