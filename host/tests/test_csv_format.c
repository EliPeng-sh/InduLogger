#include "unity.h"
#include "logger_format.h"

#include <string.h>

void setUp(void)
{
}

void tearDown(void)
{
}

void test_row_format(void)
{
  char buf[128];
  static const char *const expect =
      "1234,23.45,40.12,1013.25,0.12,-0.98,0.03\n";
  int n = logger_format_csv(buf, sizeof buf, 1234,
                            23.45f, 40.12f, 1013.25f, 0.12f, -0.98f, 0.03f);
  TEST_ASSERT_EQUAL_STRING(expect, buf);
  TEST_ASSERT_EQUAL_INT((int)strlen(expect), n);
}

void test_small_buffer_reports_error(void)
{
  char buf[8];
  TEST_ASSERT_TRUE(logger_format_csv(buf, sizeof buf, 1,
                                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f) < 0);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_row_format);
  RUN_TEST(test_small_buffer_reports_error);
  return UNITY_END();
}
