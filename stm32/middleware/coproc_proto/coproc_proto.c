#include "coproc_proto.h"

/* RED stubs: symbols exist so the suite links; behaviour tests fail. */

uint16_t cp_crc16(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
    return 0x0000u;
}

size_t cp_encode(const cp_frame_t *f, uint8_t *buf, size_t buf_sz)
{
    (void)f;
    (void)buf;
    (void)buf_sz;
    return 0u;
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
