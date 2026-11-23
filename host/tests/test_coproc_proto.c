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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_check_vector);
    RUN_TEST(test_crc_empty_input);
    RUN_TEST(test_encode_golden_heartbeat);
    RUN_TEST(test_encode_payload_layout);
    return UNITY_END();
}
