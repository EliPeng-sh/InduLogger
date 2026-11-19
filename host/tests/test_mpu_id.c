/* Host TDD for the MPU WHO_AM_I whitelist.
 * Bench board (GY-521) measures 0x70 = MPU6500 clone die; the driver previously
 * rejected it fail-fast (correct behavior for an unconfirmed ID — now confirmed). */
#include "unity.h"
#include "bsp/mpu6050/mpu6050_id.h"

void setUp(void) {}
void tearDown(void) {}

static void test_genuine_mpu6050_accepted(void)
{
  TEST_ASSERT_EQUAL_INT(1, mpu_id_accepted(0x68));
}

static void test_bench_mpu6500_accepted(void)
{
  TEST_ASSERT_EQUAL_INT(1, mpu_id_accepted(0x70)); /* board-measured 2026-11-10 */
}

static void test_unknown_ids_rejected(void)
{
  TEST_ASSERT_EQUAL_INT(0, mpu_id_accepted(0x00));
  TEST_ASSERT_EQUAL_INT(0, mpu_id_accepted(0x58)); /* BMP280 die, wrong bus mate */
  TEST_ASSERT_EQUAL_INT(0, mpu_id_accepted(0x71)); /* MPU9250 */
  TEST_ASSERT_EQUAL_INT(0, mpu_id_accepted(0xFF));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_genuine_mpu6050_accepted);
  RUN_TEST(test_bench_mpu6500_accepted);
  RUN_TEST(test_unknown_ids_rejected);
  return UNITY_END();
}
