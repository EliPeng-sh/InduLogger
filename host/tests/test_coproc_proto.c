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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_check_vector);
    RUN_TEST(test_crc_empty_input);
    return UNITY_END();
}
