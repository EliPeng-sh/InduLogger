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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_send_ack_ok_confirms);
    RUN_TEST(test_send_retransmit_on_deadline);
    RUN_TEST(test_send_fault_after_retry_max);
    RUN_TEST(test_send_reject_no_retry);
    return UNITY_END();
}
