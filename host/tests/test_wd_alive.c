#include "unity.h"
#include "stats.h"

#include <stdint.h>

void setUp(void)
{
  /* sync the monitor snapshot to the current counters so every test is
   * independent of the counters left behind by earlier tests (counters are
   * global and monotonic) */
  wd_monitor_reset();
}

void tearDown(void)
{
}

/* First check: both tasks heartbeated since the reset -> alive */
void test_all_alive_after_first_heartbeats(void)
{
  wd_heartbeat(WD_HB_SENSOR);
  wd_heartbeat(WD_HB_LOGGER);
  TEST_ASSERT_EQUAL(1, wd_all_alive());
}

/* A task that stops heartbeating freezes its counter -> not alive */
void test_frozen_heartbeat_reports_not_alive(void)
{
  wd_heartbeat(WD_HB_SENSOR);
  wd_heartbeat(WD_HB_LOGGER);
  (void)wd_all_alive(); /* baseline */
  wd_heartbeat(WD_HB_SENSOR); /* logger frozen */
  TEST_ASSERT_EQUAL(0, wd_all_alive());
}

/* Recovery: once the frozen task heartbeats again (and the others keep
 * beating), the next check reports alive again */
void test_recovered_task_reports_alive(void)
{
  wd_heartbeat(WD_HB_SENSOR);
  wd_heartbeat(WD_HB_LOGGER);
  (void)wd_all_alive(); /* baseline */
  wd_heartbeat(WD_HB_SENSOR); /* logger frozen */
  TEST_ASSERT_EQUAL(0, wd_all_alive());
  wd_heartbeat(WD_HB_SENSOR);
  wd_heartbeat(WD_HB_LOGGER); /* logger resumed */
  TEST_ASSERT_EQUAL(1, wd_all_alive());
}

/* Unknown id must be a no-op, not memory corruption */
void test_unknown_id_ignored(void)
{
  wd_heartbeat(99u);
  wd_heartbeat(WD_HB_SENSOR);
  wd_heartbeat(WD_HB_LOGGER);
  TEST_ASSERT_EQUAL(1, wd_all_alive());
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_all_alive_after_first_heartbeats);
  RUN_TEST(test_frozen_heartbeat_reports_not_alive);
  RUN_TEST(test_recovered_task_reports_alive);
  RUN_TEST(test_unknown_id_ignored);
  return UNITY_END();
}
