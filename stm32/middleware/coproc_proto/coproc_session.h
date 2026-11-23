#ifndef COPROC_SESSION_H
#define COPROC_SESSION_H

#include <stddef.h>
#include <stdint.h>
#include "coproc_proto.h"

/* ---- Tunables (M2.2 may adjust values; structure is frozen) ---- */
#define CP_ACK_TIMEOUT_MS       500u
#define CP_RETRY_MAX            3u
#define CP_HEARTBEAT_PERIOD_MS  5000u
#define CP_LINK_DOWN_MS         15000u

typedef enum { CP_LINK_UP = 0, CP_LINK_DOWN } cp_link_state_t;

typedef struct {
    cp_parser_t     parser;        /* buffered bytes across feed calls */
    /* sender (stop-and-wait) */
    uint8_t         tx_seq;
    uint8_t         pending_active;
    cp_frame_t      pending;
    uint32_t        deadline_ms;
    uint8_t         retries;
    /* receiver (dedup) */
    uint8_t         last_good_seq;
    uint8_t         seq_seen;
    /* link supervision */
    uint32_t        last_peer_ms;
    uint32_t        last_hb_ms;
    cp_link_state_t link;
    /* statistics */
    uint32_t tx_frames, tx_acked, tx_rejects, tx_retries, tx_faults;
    uint32_t rx_frames, rx_dup, rx_crc_err, rx_nacks;
} cp_session_t;   /* 1100 B on 32-bit (parser 524 + pending frame 516 + 60 B state/counters); allocate statically */

void cp_session_init(cp_session_t *s, uint32_t now_ms);

/* Queue one command frame (stop-and-wait). Emits the encoded frame at once.
 * HEARTBEAT goes fire-and-forget (never pends). CP_ERR_BUSY if a frame is
 * already pending. out_len = bytes written to buf.
 * buf must hold 8 + len bytes (CP_FRAME_MAX = 520 covers any frame);
 * CP_ERR_BUF otherwise. Callers must not send CP_CMD_ACK - that is the
 * receiver's reply frame, produced by the session layer itself. */
cp_status_t cp_session_send(cp_session_t *s, uint8_t cmd, const uint8_t *pl,
                            size_t len, uint32_t now_ms,
                            uint8_t *buf, size_t buf_sz, size_t *out_len);

/* Periodic driver. At most one emitted frame per call; retransmit has
 * priority over heartbeat. Also performs link-down supervision.
 * Returns CP_ERR_NO_FRAME when nothing was emitted.
 * buf must hold CP_FRAME_MAX (520) bytes: a retransmission repeats the
 * pending frame, which may be the largest one. */
cp_status_t cp_session_tick(cp_session_t *s, uint32_t now_ms,
                            uint8_t *buf, size_t buf_sz, size_t *out_len);

/* Feed received bytes. Processes at most one frame per call.
 * out->cmd == 0 means "nothing delivered" (frame consumed internally or
 * none completed). ack_buf/ack_len receive the encoded ACK/NACK frame
 * (0 = no ACK emitted); ack_buf must hold 10 bytes (CP_FRAME of an ACK is
 * 8 + 2 payload). consumed = bytes consumed from data. */
cp_status_t cp_session_feed(cp_session_t *s, const uint8_t *data, size_t len,
                            uint32_t now_ms, cp_frame_t *out, size_t *consumed,
                            uint8_t *ack_buf, size_t ack_buf_sz, size_t *ack_len);

#endif /* COPROC_SESSION_H */
