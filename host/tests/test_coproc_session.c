#include <string.h>
#include "unity.h"
#include "coproc_session.h"

void setUp(void)    { }
void tearDown(void) { }

static const uint8_t GOLDEN_HB[8] =
    { 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x02, 0xA4, 0x82 };

/* Build an ACK frame for feeding: [acked_seq, status]. */
static size_t make_ack(uint8_t *buf, size_t buf_sz, uint8_t acked_seq,
                       uint8_t status)
{
    cp_frame_t f;
    memset(&f, 0, sizeof(f));
    f.seq = 0u;                            /* ACK header SEQ is always 0 */
    f.cmd = CP_CMD_ACK;
    f.len = 2u;
    f.payload[0] = acked_seq;
    f.payload[1] = status;
    return cp_encode(&f, buf, buf_sz);
}

/* Test 12: send -> ACK(OK) confirms; pending cleared, seq advances. */
static void test_send_ack_ok_confirms(void)
{
    cp_session_t s;
    uint8_t txbuf[32];
    size_t n = 0u;
    cp_session_init(&s, 0u);

    cp_status_t st = cp_session_send(&s, CP_CMD_DATA_REPORT,
                                     (const uint8_t *)"xy", 2u, 1000u,
                                     txbuf, sizeof(txbuf), &n);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(10u, n);        /* 8 + 2 payload */
    TEST_ASSERT_EQUAL_UINT32(1u, s.tx_frames);
    TEST_ASSERT_EQUAL_UINT8(1u, s.pending_active);
    TEST_ASSERT_EQUAL_HEX8(0x00, txbuf[4]);       /* SEQ 0 */
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_DATA_REPORT, txbuf[5]);

    /* Sending again while pending must fail busy. */
    uint8_t b2[32];
    size_t n2 = 0u;
    TEST_ASSERT_EQUAL(CP_ERR_BUSY,
                      cp_session_send(&s, CP_CMD_DATA_REPORT,
                                      (const uint8_t *)"z", 1u, 1050u,
                                      b2, sizeof(b2), &n2));

    uint8_t ack[16];
    size_t alen = make_ack(ack, sizeof(ack), 0x00, CP_ACK_OK);
    cp_frame_t out;
    size_t consumed = 0u;
    size_t ack_out = 0u;
    st = cp_session_feed(&s, ack, alen, 1100u, &out, &consumed,
                         NULL, 0u, &ack_out);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_HEX8(0u, out.cmd);   /* ACK consumed internally */
    TEST_ASSERT_EQUAL_UINT8(0u, s.pending_active);
    TEST_ASSERT_EQUAL_UINT32(1u, s.tx_acked);
    TEST_ASSERT_EQUAL_HEX8(0x01, s.tx_seq);
}

/* Test 13: deadline -> retransmit same frame, same SEQ. */
static void test_send_retransmit_on_deadline(void)
{
    cp_session_t s;
    uint8_t txbuf[32];
    size_t n = 0u;
    cp_session_init(&s, 0u);
    TEST_ASSERT_EQUAL(CP_OK,
                      cp_session_send(&s, CP_CMD_DATA_REPORT,
                                      (const uint8_t *)"ab", 2u, 1000u,
                                      txbuf, sizeof(txbuf), &n));

    uint8_t tbuf[32];
    size_t tn = 0u;
    TEST_ASSERT_EQUAL(CP_ERR_NO_FRAME,
                      cp_session_tick(&s, 1499u, tbuf, sizeof(tbuf), &tn));

    TEST_ASSERT_EQUAL(CP_OK, cp_session_tick(&s, 1500u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL_UINT(10u, tn);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(txbuf, tbuf, 10u);   /* same bytes, same SEQ */
    TEST_ASSERT_EQUAL_UINT8(1u, s.retries);
    TEST_ASSERT_EQUAL_UINT32(1u, s.tx_retries);
}

/* Test 14: three missed deadlines -> FAULT, link down. */
static void test_send_fault_after_retry_max(void)
{
    cp_session_t s;
    uint8_t txbuf[32];
    size_t n = 0u;
    cp_session_init(&s, 0u);
    TEST_ASSERT_EQUAL(CP_OK,
                      cp_session_send(&s, CP_CMD_DATA_REPORT,
                                      (const uint8_t *)"ab", 2u, 1000u,
                                      txbuf, sizeof(txbuf), &n));

    uint8_t tbuf[32];
    size_t tn = 0u;
    TEST_ASSERT_EQUAL(CP_OK, cp_session_tick(&s, 1500u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL(CP_OK, cp_session_tick(&s, 2000u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL(CP_OK, cp_session_tick(&s, 2500u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL_UINT8(3u, s.retries);

    TEST_ASSERT_EQUAL(CP_ERR_NO_FRAME,
                      cp_session_tick(&s, 3000u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL_UINT8(0u, s.pending_active);
    TEST_ASSERT_EQUAL_UINT32(1u, s.tx_faults);
    TEST_ASSERT_EQUAL(CP_LINK_DOWN, s.link);
}

/* Test 15: ACK STATUS!=0 -> immediate reject, no retransmit. */
static void test_send_reject_no_retry(void)
{
    cp_session_t s;
    uint8_t txbuf[32];
    size_t n = 0u;
    cp_session_init(&s, 0u);
    TEST_ASSERT_EQUAL(CP_OK,
                      cp_session_send(&s, CP_CMD_DATA_REPORT,
                                      (const uint8_t *)"ab", 2u, 1000u,
                                      txbuf, sizeof(txbuf), &n));

    uint8_t ack[16];
    size_t alen = make_ack(ack, sizeof(ack), 0x00, CP_ACK_ERR_BAD_LEN);
    cp_frame_t out;
    size_t consumed = 0u;
    size_t ack_out = 0u;
    TEST_ASSERT_EQUAL(CP_OK,
                      cp_session_feed(&s, ack, alen, 1100u, &out, &consumed,
                                      NULL, 0u, &ack_out));
    TEST_ASSERT_EQUAL_UINT8(0u, s.pending_active);
    TEST_ASSERT_EQUAL_UINT32(1u, s.tx_rejects);
    TEST_ASSERT_EQUAL_HEX8(0x01, s.tx_seq);           /* advances on reject */

    uint8_t tbuf[32];
    size_t tn = 0u;
    TEST_ASSERT_EQUAL(CP_ERR_NO_FRAME,
                      cp_session_tick(&s, 1600u, tbuf, sizeof(tbuf), &tn));
}

/* Build a DATA_REPORT frame via the verified encoder. */
static size_t make_data(uint8_t *buf, size_t buf_sz, uint8_t seq,
                        const char *pl, size_t plen)
{
    cp_frame_t f;
    memset(&f, 0, sizeof(f));
    f.seq = seq;
    f.cmd = CP_CMD_DATA_REPORT;
    f.len = (uint16_t)plen;
    memcpy(f.payload, pl, plen);
    return cp_encode(&f, buf, buf_sz);
}

/* Test 16: duplicate SEQ -> re-ACK, no delivery. */
static void test_rx_dedup_reacks(void)
{
    cp_session_t s;
    uint8_t frame[16];
    size_t flen = make_data(frame, sizeof(frame), 5u, "xy", 2u);
    cp_session_init(&s, 0u);

    cp_frame_t out;
    size_t consumed = 0u;
    uint8_t ack[16];
    size_t ack_len = 0u;
    cp_status_t st = cp_session_feed(&s, frame, flen, 100u, &out, &consumed,
                                     ack, sizeof(ack), &ack_len);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_DATA_REPORT, out.cmd);   /* delivered */
    TEST_ASSERT_EQUAL_HEX8(0x05, out.seq);
    TEST_ASSERT_EQUAL_UINT(10u, ack_len);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_ACK, ack[5]);
    TEST_ASSERT_EQUAL_HEX8(0x05, ack[6]);
    TEST_ASSERT_EQUAL_HEX8(CP_ACK_OK, ack[7]);

    st = cp_session_feed(&s, frame, flen, 200u, &out, &consumed,
                         ack, sizeof(ack), &ack_len);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_HEX8(0u, out.cmd);                   /* not delivered */
    TEST_ASSERT_EQUAL_UINT32(1u, s.rx_dup);
    TEST_ASSERT_EQUAL_UINT(10u, ack_len);                  /* re-ACKed */
}

/* Test 17: 15 s peer silence -> link down. */
static void test_rx_link_down_after_silence(void)
{
    cp_session_t s;
    cp_session_init(&s, 0u);
    uint8_t tbuf[32];
    size_t tn = 0u;
    (void)cp_session_tick(&s, 14999u, tbuf, sizeof(tbuf), &tn);
    TEST_ASSERT_EQUAL(CP_LINK_UP, s.link);
    (void)cp_session_tick(&s, 15000u, tbuf, sizeof(tbuf), &tn);
    TEST_ASSERT_EQUAL(CP_LINK_DOWN, s.link);

    /* any valid frame revives the link */
    cp_frame_t out;
    size_t consumed = 0u;
    TEST_ASSERT_EQUAL(CP_OK,
                      cp_session_feed(&s, GOLDEN_HB, sizeof(GOLDEN_HB),
                                      15100u, &out, &consumed,
                                      NULL, 0u, NULL));
    TEST_ASSERT_EQUAL(CP_LINK_UP, s.link);
}

/* Test 18: 1 KB pseudo-random garbage -> defined results only, then
 * normal operation resumes. */
static uint32_t g_xs;
static uint8_t xorshift_byte(void)
{
    g_xs ^= g_xs << 13;
    g_xs ^= g_xs >> 17;
    g_xs ^= g_xs << 5;
    return (uint8_t)g_xs;
}

static void test_rx_fuzz_survives(void)
{
    cp_session_t s;
    cp_session_init(&s, 0u);
    g_xs = 0x12345678u;                    /* fixed seed -> deterministic */

    static uint8_t chunk[7];
    for (size_t i = 0; i < 1024u / 7u; i++) {
        for (size_t j = 0; j < 7u; j++) {
            chunk[j] = xorshift_byte();
        }
        cp_frame_t out;
        size_t consumed = 0u;
        cp_status_t st = cp_session_feed(&s, chunk, sizeof(chunk), 1000u + i,
                                         &out, &consumed, NULL, 0u, NULL);
        TEST_ASSERT_TRUE(st == CP_OK || st == CP_ERR_NO_FRAME ||
                         st == CP_ERR_CRC || st == CP_ERR_LEN);
    }

    /* Drain: 520 zero bytes force any phantom frame to complete and
     * CRC-fail, deterministically resetting the parser. */
    static uint8_t drain[520];
    memset(drain, 0, sizeof(drain));
    cp_frame_t out;
    size_t consumed = 0u;
    (void)cp_session_feed(&s, drain, sizeof(drain), 2000u, &out, &consumed,
                          NULL, 0u, NULL);

    /* golden heartbeat is consumed silently */
    consumed = 0u;
    cp_status_t st = cp_session_feed(&s, GOLDEN_HB, sizeof(GOLDEN_HB), 2100u,
                                     &out, &consumed, NULL, 0u, NULL);
    TEST_ASSERT_TRUE(st == CP_OK || st == CP_ERR_CRC);
    TEST_ASSERT_EQUAL_HEX8(0u, out.cmd);

    /* a fresh data frame is delivered normally */
    uint8_t frame[16];
    size_t flen = make_data(frame, sizeof(frame), 9u, "ok", 2u);
    uint8_t ack[16];
    size_t ack_len = 0u;
    st = cp_session_feed(&s, frame, flen, 2200u, &out, &consumed,
                         ack, sizeof(ack), &ack_len);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_DATA_REPORT, out.cmd);
    TEST_ASSERT_EQUAL_UINT(10u, ack_len);
}

/* Test 19: heartbeat emission every 5 s. */
static void test_heartbeat_periodic(void)
{
    cp_session_t s;
    cp_session_init(&s, 0u);
    uint8_t tbuf[32];
    size_t tn = 0u;

    TEST_ASSERT_EQUAL(CP_OK, cp_session_tick(&s, 5000u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL_UINT(8u, tn);        /* empty HEARTBEAT frame */
    TEST_ASSERT_EQUAL_HEX8(CP_SOF0, tbuf[0]);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, tbuf[5]);

    TEST_ASSERT_EQUAL(CP_ERR_NO_FRAME,
                      cp_session_tick(&s, 9999u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL(CP_OK, cp_session_tick(&s, 10000u, tbuf, sizeof(tbuf), &tn));
    TEST_ASSERT_EQUAL_UINT(8u, tn);
}

/* Test 20: unknown CMD with valid CRC -> NACK, not delivered. */
static void test_rx_unknown_cmd_nacks(void)
{
    cp_session_t s;
    cp_frame_t f;
    uint8_t frame[16];
    memset(&f, 0, sizeof(f));
    f.seq = 3u;
    f.cmd = 0x7Fu;                         /* not in the v1 CMD set */
    f.len = 1u;
    f.payload[0] = 0xAAu;
    size_t flen = cp_encode(&f, frame, sizeof(frame));
    TEST_ASSERT_TRUE(flen > 0u);

    cp_session_init(&s, 0u);
    cp_frame_t out;
    size_t consumed = 0u;
    uint8_t ack[16];
    size_t ack_len = 0u;
    cp_status_t st = cp_session_feed(&s, frame, flen, 100u, &out, &consumed,
                                     ack, sizeof(ack), &ack_len);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_HEX8(0u, out.cmd);                   /* not delivered */
    TEST_ASSERT_EQUAL_UINT32(1u, s.rx_nacks);
    TEST_ASSERT_EQUAL_UINT(10u, ack_len);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_ACK, ack[5]);
    TEST_ASSERT_EQUAL_HEX8(0x03, ack[6]);                  /* acked SEQ 3 */
    TEST_ASSERT_EQUAL_HEX8(CP_ACK_ERR_UNKNOWN_CMD, ack[7]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_send_ack_ok_confirms);
    RUN_TEST(test_send_retransmit_on_deadline);
    RUN_TEST(test_send_fault_after_retry_max);
    RUN_TEST(test_send_reject_no_retry);
    RUN_TEST(test_rx_dedup_reacks);
    RUN_TEST(test_rx_link_down_after_silence);
    RUN_TEST(test_rx_fuzz_survives);
    RUN_TEST(test_heartbeat_periodic);
    RUN_TEST(test_rx_unknown_cmd_nacks);
    return UNITY_END();
}
