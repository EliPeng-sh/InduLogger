#include "unity.h"
#include "sensor_check.h"

void setUp(void) {}
void tearDown(void) {}

void test_bme_valid(void)     { TEST_ASSERT_TRUE(sensor_plausible_bme(23.5f, 45.0f, 1013.0f)); }
void test_bme_temp_range(void) { TEST_ASSERT_FALSE(sensor_plausible_bme(-41.0f, 45.0f, 1013.0f)); }
void test_bme_hum_range(void)  { TEST_ASSERT_FALSE(sensor_plausible_bme(25.0f, 101.0f, 1013.0f)); }
void test_mpu_rest(void)       { TEST_ASSERT_TRUE(sensor_plausible_mpu(0.10f, -0.05f, 0.98f)); }
void test_mpu_flat(void)       { TEST_ASSERT_FALSE(sensor_plausible_mpu(5.0f, 0.0f, 0.0f)); }

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_bme_valid);
  RUN_TEST(test_bme_temp_range);
  RUN_TEST(test_bme_hum_range);
  RUN_TEST(test_mpu_rest);
  RUN_TEST(test_mpu_flat);
  return UNITY_END();
}
