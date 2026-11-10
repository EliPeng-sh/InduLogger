/* Host TDD for the BME280 stuck-bus recovery decision logic.
 * Bug being guarded: a wedged slave FSM holds SDA low forever (board-proven);
 * recovery must trigger after a bounded failure streak, then re-arm cleanly. */
#include "unity.h"
#include "bsp/bme280/bme280_recovery.h"

void setUp(void) {}
void tearDown(void) {}

static void fail_n(bme280_recovery_t *r, unsigned n, int expect_trigger_on_last)
{
  for (unsigned i = 1; i <= n; i++) {
    int triggered = bme280_recovery_on_fail(r);
    if (i < n) {
      TEST_ASSERT_EQUAL_INT(0, triggered);
    } else {
      TEST_ASSERT_EQUAL_INT(expect_trigger_on_last, triggered);
    }
  }
}

static void test_fails_below_threshold_do_not_trigger(void)
{
  bme280_recovery_t r;
  bme280_recovery_init(&r, 5);
  fail_n(&r, 4, 0); /* 4 fails < 5: no trigger yet (and loop asserts 0s) */
}

static void test_nth_fail_triggers_and_rearms(void)
{
  bme280_recovery_t r;
  bme280_recovery_init(&r, 5);
  fail_n(&r, 5, 1);       /* 5th fail triggers */
  fail_n(&r, 4, 0);       /* streak reset after trigger */
  TEST_ASSERT_EQUAL_INT(1, bme280_recovery_on_fail(&r)); /* fresh 5th triggers again */
}

static void test_success_resets_streak_exact(void)
{
  bme280_recovery_t r;
  bme280_recovery_init(&r, 5);
  fail_n(&r, 4, 0);
  bme280_recovery_on_success(&r); /* streak must go back to zero */
  fail_n(&r, 4, 0);
  TEST_ASSERT_EQUAL_INT(1, bme280_recovery_on_fail(&r));
}

static void test_threshold_one_triggers_every_fail(void)
{
  bme280_recovery_t r;
  bme280_recovery_init(&r, 1);
  TEST_ASSERT_EQUAL_INT(1, bme280_recovery_on_fail(&r));
  TEST_ASSERT_EQUAL_INT(1, bme280_recovery_on_fail(&r)); /* re-armed each time */
}

static void test_init_resets_state(void)
{
  bme280_recovery_t r;
  bme280_recovery_init(&r, 5);
  fail_n(&r, 4, 0);
  bme280_recovery_init(&r, 3); /* re-init must discard old streak */
  fail_n(&r, 2, 0);
  TEST_ASSERT_EQUAL_INT(1, bme280_recovery_on_fail(&r));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_fails_below_threshold_do_not_trigger);
  RUN_TEST(test_nth_fail_triggers_and_rearms);
  RUN_TEST(test_success_resets_streak_exact);
  RUN_TEST(test_threshold_one_triggers_every_fail);
  RUN_TEST(test_init_resets_state);
  return UNITY_END();
}
