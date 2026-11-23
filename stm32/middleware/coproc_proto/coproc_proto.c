#include "coproc_proto.h"

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
    if (p == NULL) {
        return;
    }
    p->state = CP_P_SOF0;
    p->hdr[0] = 0u;
    p->hdr[1] = 0u;
    p->hdr[2] = 0u;
    p->hdr[3] = 0u;
    p->len = 0u;
    p->got = 0u;
}

cp_status_t cp_parse(cp_parser_t *p, const uint8_t *data, size_t len,
                     cp_frame_t *out, size_t *consumed)
{
    if (p == NULL || data == NULL || consumed == NULL) {
        return CP_ERR_ARG;
    }
    *consumed = 0u;

    for (size_t i = 0; i < len; i++) {
        const uint8_t b = data[i];
        (*consumed)++;

        switch (p->state) {
        case CP_P_SOF0:
            if (b == CP_SOF0) {
                p->state = CP_P_SOF1;
            }
            break;

        case CP_P_SOF1:
            if (b == CP_SOF1) {
                p->state = CP_P_LEN_H;
            } else if (b == CP_SOF0) {
                p->state = CP_P_SOF1;      /* stay hunting */
            } else {
                p->state = CP_P_SOF0;
            }
            break;

        case CP_P_LEN_H:
            p->hdr[0] = b;
            p->state = CP_P_LEN_L;
            break;

        case CP_P_LEN_L:
            p->hdr[1] = b;
            p->len = (uint16_t)(((uint16_t)p->hdr[0] << 8) | p->hdr[1]);
            if (p->len > CP_PAYLOAD_MAX) {
                cp_parser_init(p);         /* drop, resync at LEN boundary */
                return CP_ERR_LEN;
            }
            p->state = CP_P_SEQ;
            break;

        case CP_P_SEQ:
            p->hdr[2] = b;
            p->state = CP_P_CMD;
            break;

        case CP_P_CMD:
            p->hdr[3] = b;
            p->got = 0u;
            p->state = (p->len == 0u) ? CP_P_CRC_H : CP_P_PAYLOAD;
            break;

        case CP_P_PAYLOAD:
            p->payload[p->got] = b;
            p->got++;
            if (p->got == p->len) {
                p->state = CP_P_CRC_H;
            }
            break;

        case CP_P_CRC_H:
            p->hdr[0] = b;                 /* LEN_H no longer needed */
            p->state = CP_P_CRC_L;
            break;

        case CP_P_CRC_L: {
            const uint16_t rx = (uint16_t)(((uint16_t)p->hdr[0] << 8) | b);
            const uint8_t  seq = p->hdr[2];
            const uint8_t  cmd = p->hdr[3];
            const uint16_t plen = p->len;
            uint8_t hdr4[4] = {
                (uint8_t)(plen >> 8), (uint8_t)(plen & 0xFFu), seq, cmd
            };
            uint16_t calc = crc_update(crc_update(0xFFFFu, hdr4, 4u),
                                       p->payload, plen);
            cp_parser_init(p);
            if (rx != calc) {
                return CP_ERR_CRC;         /* whole frame consumed */
            }
            if (out != NULL) {
                out->seq = seq;
                out->cmd = cmd;
                out->len = plen;
                for (uint16_t k = 0; k < plen; k++) {
                    out->payload[k] = p->payload[k];
                }
            }
            return CP_OK;
        }

        default:
            cp_parser_init(p);
            break;
        }
    }
    return CP_ERR_NO_FRAME;                /* buffered, caller re-feeds rest */
}
