#include "unity.h"
#include "stats.h"

void setUp(void) {}
void tearDown(void) {}

void test_bor_flag(void)  { TEST_ASSERT_EQUAL_STRING("brownout", reset_cause_name(RESET_CAUSE_BOR)); }
void test_iwdg_flag(void) { TEST_ASSERT_EQUAL_STRING("iwdg", reset_cause_name(RESET_CAUSE_IWDG)); }
void test_unknown(void)   { TEST_ASSERT_EQUAL_STRING("unknown", reset_cause_name(RESET_CAUSE_UNKNOWN)); }

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_bor_flag);
  RUN_TEST(test_iwdg_flag);
  RUN_TEST(test_unknown);
  return UNITY_END();
}
