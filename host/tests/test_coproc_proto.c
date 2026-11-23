#include <string.h>
#include "unity.h"
#include "coproc_proto.h"

void setUp(void)    { }
void tearDown(void) { }

/* Golden vector verified with an independent Python oracle:
 * CRC-16/CCITT-False over {00 00 00 02} = 0xA482. */
static const uint8_t GOLDEN_HB[8] =
    { 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x02, 0xA4, 0x82 };

/* Test 1: CRC-16/CCITT-False check vector. */
static void test_crc_check_vector(void)
{
    uint16_t crc = cp_crc16((const uint8_t *)"123456789", 9u);
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc);
}

/* Test 2: empty input keeps the init value 0xFFFF. */
static void test_crc_empty_input(void)
{
    uint8_t b = 0x00;
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, cp_crc16(&b, 0u));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, cp_crc16(NULL, 0u));
}

/* Test 3: encode the golden heartbeat frame byte-for-byte. */
static void test_encode_golden_heartbeat(void)
{
    cp_frame_t f;
    uint8_t buf[16];
    memset(&f, 0, sizeof(f));
    f.seq = 0u;
    f.cmd = CP_CMD_HEARTBEAT;
    f.len = 0u;
    size_t n = cp_encode(&f, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(8u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(GOLDEN_HB, buf, 8);
}

/* Test 4: payload framing, CRC placement, buffer-too-small. */
static void test_encode_payload_layout(void)
{
    cp_frame_t f;
    uint8_t buf[16];
    memset(&f, 0, sizeof(f));
    f.seq = 0x07u;
    f.cmd = CP_CMD_DATA_REPORT;
    f.len = 3u;
    f.payload[0] = 0x01u;
    f.payload[1] = 0x02u;
    f.payload[2] = 0x03u;
    size_t n = cp_encode(&f, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(11u, n);
    TEST_ASSERT_EQUAL_HEX8(0xA5, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x07, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_DATA_REPORT, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[7]);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[8]);
    uint16_t crc = cp_crc16(&buf[2], 7u);        /* LEN_H .. payload end */
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), buf[9]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc & 0xFFu), buf[10]);
    TEST_ASSERT_EQUAL_UINT(0u, cp_encode(&f, buf, 10u));  /* 11 needed */
    TEST_ASSERT_EQUAL_UINT(0u, cp_encode(NULL, buf, sizeof(buf)));
}

/* Test 5: golden frame in one shot. */
static void test_parse_golden_one_shot(void)
{
    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    cp_status_t st = cp_parse(&p, GOLDEN_HB, sizeof(GOLDEN_HB), &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(8u, consumed);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.seq);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, out.cmd);
    TEST_ASSERT_EQUAL_UINT16(0u, out.len);
}

/* Test 6: byte-by-byte feed must buffer across calls. */
static void test_parse_byte_by_byte(void)
{
    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    for (size_t i = 0; i < 7u; i++) {
        cp_status_t st = cp_parse(&p, &GOLDEN_HB[i], 1u, &out, &consumed);
        TEST_ASSERT_EQUAL(CP_ERR_NO_FRAME, st);
        TEST_ASSERT_EQUAL_UINT(1u, consumed);
    }
    cp_status_t st = cp_parse(&p, &GOLDEN_HB[7], 1u, &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(1u, consumed);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, out.cmd);
}

/* Test 7: noise wrapping the frame (contains an unpaired 0xA5). */
static void test_parse_noise_wrapped(void)
{
    static const uint8_t noise_and_frame[12] = {
        0x11, 0x22, 0xA5, 0x33,                     /* noise, unpaired SOF0 */
        0xA5, 0x5A, 0x00, 0x00, 0x00, 0x02, 0xA4, 0x82
    };
    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    cp_status_t st = cp_parse(&p, noise_and_frame, sizeof(noise_and_frame),
                              &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(12u, consumed);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, out.cmd);
}

/* Test 8: payload containing the SOF pair must not re-sync. */
static void test_parse_payload_contains_sof(void)
{
    cp_frame_t f;
    uint8_t buf[16];
    memset(&f, 0, sizeof(f));
    f.seq = 1u;
    f.cmd = CP_CMD_DATA_REPORT;
    f.len = 4u;
    f.payload[0] = 0xA5u;
    f.payload[1] = 0x5Au;
    f.payload[2] = 0x7Eu;
    f.payload[3] = 0x00u;
    size_t n = cp_encode(&f, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(12u, n);

    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    cp_status_t st = cp_parse(&p, buf, n, &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(n, consumed);
    TEST_ASSERT_EQUAL_UINT16(4u, out.len);
    TEST_ASSERT_EQUAL_HEX8(0xA5, out.payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, out.payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0x7E, out.payload[2]);
}

/* Test 9: corrupted CRC -> dropped, parser reusable immediately. */
static void test_parse_crc_corrupt_drops(void)
{
    uint8_t bad[8];
    memcpy(bad, GOLDEN_HB, sizeof(bad));
    bad[7] ^= 0xFFu;

    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    cp_status_t st = cp_parse(&p, bad, sizeof(bad), &out, &consumed);
    TEST_ASSERT_EQUAL(CP_ERR_CRC, st);
    TEST_ASSERT_EQUAL_UINT(8u, consumed);

    st = cp_parse(&p, GOLDEN_HB, sizeof(GOLDEN_HB), &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, out.cmd);
}

/* Test 10: over-length LEN -> CP_ERR_LEN consumed through LEN_L (4 bytes),
 * parser reset; re-feed skips the garbage, next frame parses. */
static void test_parse_oversize_len(void)
{
    static const uint8_t data[18] = {
        0xA5, 0x5A, 0xFF, 0xFF, 0x00, 0x02,        /* LEN=0xFFFF > 512 */
        0x11, 0x22, 0x33, 0x44,                    /* garbage tail     */
        0xA5, 0x5A, 0x00, 0x00, 0x00, 0x02, 0xA4, 0x82
    };
    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    cp_status_t st = cp_parse(&p, data, sizeof(data), &out, &consumed);
    TEST_ASSERT_EQUAL(CP_ERR_LEN, st);
    TEST_ASSERT_EQUAL_UINT(4u, consumed);          /* SOF0,SOF1,LEN_H,LEN_L */

    st = cp_parse(&p, data + consumed, sizeof(data) - consumed, &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(14u, consumed);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, out.cmd);
}

/* Test 11: two frames back-to-back -> two deliveries. */
static void test_parse_two_frames_back_to_back(void)
{
    uint8_t stream[8 + 12];
    memcpy(stream, GOLDEN_HB, 8u);

    cp_frame_t f2;
    memset(&f2, 0, sizeof(f2));
    f2.seq = 1u;
    f2.cmd = CP_CMD_DATA_REPORT;
    f2.len = 4u;
    f2.payload[0] = 'a';
    f2.payload[1] = 'b';
    f2.payload[2] = 'c';
    f2.payload[3] = 'd';
    size_t n2 = cp_encode(&f2, stream + 8, sizeof(stream) - 8u);
    TEST_ASSERT_EQUAL_UINT(12u, n2);

    cp_parser_t p;
    cp_frame_t  out;
    size_t consumed = 0u;
    cp_parser_init(&p);
    cp_status_t st = cp_parse(&p, stream, sizeof(stream), &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(8u, consumed);
    TEST_ASSERT_EQUAL_HEX8(CP_CMD_HEARTBEAT, out.cmd);

    st = cp_parse(&p, stream + consumed, sizeof(stream) - consumed,
                  &out, &consumed);
    TEST_ASSERT_EQUAL(CP_OK, st);
    TEST_ASSERT_EQUAL_UINT(12u, consumed);
    TEST_ASSERT_EQUAL_HEX8('a', out.payload[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_check_vector);
    RUN_TEST(test_crc_empty_input);
    RUN_TEST(test_encode_golden_heartbeat);
    RUN_TEST(test_encode_payload_layout);
    RUN_TEST(test_parse_golden_one_shot);
    RUN_TEST(test_parse_byte_by_byte);
    RUN_TEST(test_parse_noise_wrapped);
    RUN_TEST(test_parse_payload_contains_sof);
    RUN_TEST(test_parse_crc_corrupt_drops);
    RUN_TEST(test_parse_oversize_len);
    RUN_TEST(test_parse_two_frames_back_to_back);
    return UNITY_END();
}
