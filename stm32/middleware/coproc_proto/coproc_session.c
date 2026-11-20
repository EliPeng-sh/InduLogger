#include "coproc_session.h"

/* RED stubs: symbols exist so the suite links; behaviour tests fail. */

void cp_session_init(cp_session_t *s, uint32_t now_ms)
{
    (void)s;
    (void)now_ms;
}

cp_status_t cp_session_send(cp_session_t *s, uint8_t cmd, const uint8_t *pl,
                            size_t len, uint32_t now_ms,
                            uint8_t *buf, size_t buf_sz, size_t *out_len)
{
    (void)s; (void)cmd; (void)pl; (void)len; (void)now_ms; (void)buf; (void)buf_sz;
    if (out_len != NULL) {
        *out_len = 0u;
    }
    return CP_ERR_ARG;
}

cp_status_t cp_session_tick(cp_session_t *s, uint32_t now_ms,
                            uint8_t *buf, size_t buf_sz, size_t *out_len)
{
    (void)s; (void)now_ms; (void)buf; (void)buf_sz;
    if (out_len != NULL) {
        *out_len = 0u;
    }
    return CP_ERR_ARG;
}

cp_status_t cp_session_feed(cp_session_t *s, const uint8_t *data, size_t len,
                            uint32_t now_ms, cp_frame_t *out, size_t *consumed,
                            uint8_t *ack_buf, size_t ack_buf_sz, size_t *ack_len)
{
    (void)s; (void)data; (void)len; (void)now_ms; (void)out; (void)ack_buf;
    (void)ack_buf_sz;
    if (consumed != NULL) {
        *consumed = 0u;
    }
    if (ack_len != NULL) {
        *ack_len = 0u;
    }
    return CP_ERR_ARG;
}
