#include "unity.h"
#include "bme280/bme280_compensate.h"

/* Bosch datasheet (BST-BME280-DS002) worked example, section 4.2.3:
 * adc_T=519888 adc_P=415148 with the datasheet trims
 *   -> T = 25.08 C (t_fine = 128422), P = 100653.27 Pa = 1006.53 hPa.
 * Humidity vectors are from a real Bosch chip cross-checked against the
 * official BME280_driver v3.5.1 double implementation (43.2931 %rH). */

static bme280_trim_t tr;

void setUp(void)
{
  tr.T1 = 27504;  tr.T2 = 26435;  tr.T3 = -1000;
  tr.P1 = 36477;  tr.P2 = -10685; tr.P3 = 3024;
  tr.P4 = 2855;   tr.P5 = 140;    tr.P6 = -7;
  tr.P7 = 15500;  tr.P8 = -14600; tr.P9 = 6000;
  tr.H1 = 75;     tr.H2 = 364;    tr.H3 = 0;
  tr.H4 = 313;    tr.H5 = 0;      tr.H6 = 30;
}

void tearDown(void) {}

void test_temp_datasheet_example(void)
{
  int32_t t_fine = 0;
  int32_t cC = bme280_compensate_temp(&tr, 519888, &t_fine);
  TEST_ASSERT_EQUAL_INT32(2508, cC);        /* 25.08 C */
  TEST_ASSERT_EQUAL_INT32(128422, t_fine);  /* documented fine value */
}

void test_press_datasheet_example(void)
{
  int32_t t_fine;
  (void)bme280_compensate_temp(&tr, 519888, &t_fine);
  float hpa = bme280_compensate_press(&tr, 415148, t_fine);
  /* 100653.27 Pa = 1006.53 hPa (regression for the missing /256 (Q24.8) + P7 bug) */
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1006.53f, hpa);
}

void test_press_failsafe_degenerate_p1(void)
{
  bme280_trim_t bad = tr;
  bad.P1 = 0; /* degenerate trims: must fail safe, not divide by zero */
  TEST_ASSERT_EQUAL_FLOAT(0.0f, bme280_compensate_press(&bad, 415148, 128422));
}

void test_hum_official_oracle(void)
{
  int32_t t_fine;
  (void)bme280_compensate_temp(&tr, 519888, &t_fine);
  /* this H vector needs its own t_fine: temp=23.782 C -> t_fine=121763 */
  float rh = bme280_compensate_hum(&tr, 27721, 121763);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 43.29f, rh);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_temp_datasheet_example);
  RUN_TEST(test_press_datasheet_example);
  RUN_TEST(test_press_failsafe_degenerate_p1);
  RUN_TEST(test_hum_official_oracle);
  return UNITY_END();
}
