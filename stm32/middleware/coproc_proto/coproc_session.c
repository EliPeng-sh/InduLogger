#include "coproc_session.h"
#include <string.h>

static void emit_heartbeat(cp_session_t *s, uint32_t now_ms,
                           uint8_t *buf, size_t buf_sz, size_t *out_len)
{
    cp_frame_t hb;
    hb.seq = 0u;
    hb.cmd = CP_CMD_HEARTBEAT;
    hb.len = 0u;
    size_t n = cp_encode(&hb, buf, buf_sz);
    if (n == 0u) {
        *out_len = 0u;
        return;
    }
    s->last_hb_ms = now_ms;
    *out_len = n;
}

static void emit_ack(uint8_t acked_seq, uint8_t status,
                     uint8_t *ack_buf, size_t ack_buf_sz, size_t *ack_len)
{
    if (ack_buf == NULL || ack_len == NULL) {
        return;
    }
    cp_frame_t ack;
    ack.seq = 0u;                          /* ACK header SEQ is always 0 */
    ack.cmd = CP_CMD_ACK;
    ack.len = 2u;
    ack.payload[0] = acked_seq;
    ack.payload[1] = status;
    size_t n = cp_encode(&ack, ack_buf, ack_buf_sz);
    *ack_len = (n > 0u) ? n : 0u;
}

static int cmd_is_known(uint8_t cmd)
{
    switch (cmd) {
    case CP_CMD_PING:
    case CP_CMD_DATA_REPORT:
    case CP_CMD_DIAG_REPORT:
    case CP_CMD_MQTT_PUB:
    case CP_CMD_MQTT_SUB:
    case CP_CMD_WIFI_CONFIG_SET:
    case CP_CMD_WIFI_CONFIG_GET:
    case CP_CMD_NET_STATUS:
    case CP_CMD_TIME_SET:
        return 1;
    default:
        return 0;
    }
}

void cp_session_init(cp_session_t *s, uint32_t now_ms)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->last_peer_ms = now_ms;
    s->last_hb_ms   = now_ms;
    s->link         = CP_LINK_UP;
}

cp_status_t cp_session_send(cp_session_t *s, uint8_t cmd, const uint8_t *pl,
                            size_t len, uint32_t now_ms,
                            uint8_t *buf, size_t buf_sz, size_t *out_len)
{
    if (s == NULL || buf == NULL || out_len == NULL) {
        return CP_ERR_ARG;
    }
    if (pl == NULL && len > 0u) {
        return CP_ERR_ARG;
    }
    if (len > CP_PAYLOAD_MAX) {
        return CP_ERR_LEN;
    }
    *out_len = 0u;
    if (s->pending_active) {
        return CP_ERR_BUSY;
    }
    if (cmd == CP_CMD_HEARTBEAT) {
        emit_heartbeat(s, now_ms, buf, buf_sz, out_len);
        return (*out_len > 0u) ? CP_OK : CP_ERR_BUF;
    }

    s->pending.seq = s->tx_seq;
    s->pending.cmd = cmd;
    s->pending.len = (uint16_t)len;
    if (len > 0u) {
        memcpy(s->pending.payload, pl, len);
    }
    s->pending_active = 1u;
    s->deadline_ms    = now_ms + CP_ACK_TIMEOUT_MS;
    s->retries        = 0u;

    size_t n = cp_encode(&s->pending, buf, buf_sz);
    if (n == 0u) {
        s->pending_active = 0u;
        return CP_ERR_BUF;
    }
    s->tx_frames++;
    *out_len = n;
    return CP_OK;
}

cp_status_t cp_session_tick(cp_session_t *s, uint32_t now_ms,
                            uint8_t *buf, size_t buf_sz, size_t *out_len)
{
    if (s == NULL || buf == NULL || out_len == NULL) {
        return CP_ERR_ARG;
    }
    *out_len = 0u;

    /* 1. retransmit due (priority over heartbeat) */
    if (s->pending_active && now_ms >= s->deadline_ms) {
        if (s->retries < CP_RETRY_MAX) {
            s->retries++;
            s->tx_retries++;
            s->deadline_ms = now_ms + CP_ACK_TIMEOUT_MS;
            size_t n = cp_encode(&s->pending, buf, buf_sz);
            if (n == 0u) {
                return CP_ERR_BUF;
            }
            *out_len = n;
            return CP_OK;
        }
        /* retries exhausted -> FAULT: drop frame, link down */
        s->pending_active = 0u;
        s->tx_faults++;
        s->link = CP_LINK_DOWN;
        return CP_ERR_NO_FRAME;
    }

    /* 2. link supervision: peer silent too long */
    if (now_ms - s->last_peer_ms >= CP_LINK_DOWN_MS) {
        s->link = CP_LINK_DOWN;
    }

    /* 3. heartbeat probe */
    if (now_ms - s->last_hb_ms >= CP_HEARTBEAT_PERIOD_MS) {
        emit_heartbeat(s, now_ms, buf, buf_sz, out_len);
        return (*out_len > 0u) ? CP_OK : CP_ERR_NO_FRAME;
    }
    return CP_ERR_NO_FRAME;
}

cp_status_t cp_session_feed(cp_session_t *s, const uint8_t *data, size_t len,
                            uint32_t now_ms, cp_frame_t *out, size_t *consumed,
                            uint8_t *ack_buf, size_t ack_buf_sz, size_t *ack_len)
{
    if (s == NULL || data == NULL || consumed == NULL) {
        return CP_ERR_ARG;
    }
    if (ack_len != NULL) {
        *ack_len = 0u;
    }
    if (out != NULL) {
        out->cmd = 0u;                     /* "nothing delivered" sentinel */
    }

    cp_frame_t f;
    size_t used = 0u;
    cp_status_t st = cp_parse(&s->parser, data, len, &f, &used);
    *consumed = used;

    if (st == CP_ERR_NO_FRAME) {
        return CP_ERR_NO_FRAME;
    }
    if (st == CP_ERR_CRC) {
        s->rx_crc_err++;
        return CP_ERR_CRC;
    }
    if (st == CP_ERR_LEN) {
        s->rx_crc_err++;                   /* malformed header, same bucket */
        return CP_ERR_LEN;
    }
    if (st != CP_OK) {
        return st;
    }

    s->rx_frames++;
    s->last_peer_ms = now_ms;
    if (s->link == CP_LINK_DOWN) {
        s->link = CP_LINK_UP;
    }

    if (f.cmd == CP_CMD_ACK) {
        if (f.len == 2u && s->pending_active &&
            f.payload[0] == s->pending.seq) {
            s->pending_active = 0u;
            if (f.payload[1] == CP_ACK_OK) {
                s->tx_acked++;
            } else {
                s->tx_rejects++;           /* immediate fail, no retry */
            }
            s->tx_seq++;
        }
        return CP_OK;                      /* stale/foreign ACK: ignored */
    }

    if (f.cmd == CP_CMD_HEARTBEAT) {
        return CP_OK;                      /* probe: silent, refreshes peer */
    }

    if (!cmd_is_known(f.cmd)) {
        s->rx_nacks++;
        emit_ack(f.seq, CP_ACK_ERR_UNKNOWN_CMD, ack_buf, ack_buf_sz, ack_len);
        return CP_OK;
    }

    if (s->seq_seen && f.seq == s->last_good_seq) {
        s->rx_dup++;                       /* duplicate: no delivery */
    } else {
        s->last_good_seq = f.seq;
        s->seq_seen = 1u;
        if (out != NULL) {
            *out = f;                      /* deliver */
        }
    }
    emit_ack(f.seq, CP_ACK_OK, ack_buf, ack_buf_sz, ack_len);
    return CP_OK;
}
