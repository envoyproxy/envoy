#include "source/extensions/tracers/datadog/tracer_stats.h"

#include "test/common/stats/stat_test_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

TEST(DatadogTracerTracerStatsTest, TracerStats) {
  Stats::TestUtil::TestStore store;
  TracerStats stats = makeTracerStats(*store.rootScope());

  // Verify the names.
  EXPECT_EQ(stats.reports_skipped_no_cluster_.name(), "tracing.datadog.reports_skipped_no_cluster");
  EXPECT_EQ(stats.reports_sent_.name(), "tracing.datadog.reports_sent");
  EXPECT_EQ(stats.reports_dropped_.name(), "tracing.datadog.reports_dropped");
  EXPECT_EQ(stats.reports_failed_.name(), "tracing.datadog.reports_failed");

  // Counters begin at zero.
  EXPECT_EQ(0, store.counter("tracing.datadog.reports_skipped_no_cluster").value());
  EXPECT_EQ(0, store.counter("tracing.datadog.reports_sent").value());
  EXPECT_EQ(0, store.counter("tracing.datadog.reports_dropped").value());
  EXPECT_EQ(0, store.counter("tracing.datadog.reports_failed").value());

  // Increments are reflected in the value.
  stats.reports_skipped_no_cluster_.inc();
  EXPECT_EQ(1, store.counter("tracing.datadog.reports_skipped_no_cluster").value());
  stats.reports_sent_.inc();
  EXPECT_EQ(1, store.counter("tracing.datadog.reports_sent").value());
  stats.reports_dropped_.inc();
  EXPECT_EQ(1, store.counter("tracing.datadog.reports_dropped").value());
  stats.reports_failed_.inc();
  EXPECT_EQ(1, store.counter("tracing.datadog.reports_failed").value());

  // And again.
  stats.reports_skipped_no_cluster_.inc();
  EXPECT_EQ(2, store.counter("tracing.datadog.reports_skipped_no_cluster").value());
  stats.reports_sent_.inc();
  EXPECT_EQ(2, store.counter("tracing.datadog.reports_sent").value());
  stats.reports_dropped_.inc();
  EXPECT_EQ(2, store.counter("tracing.datadog.reports_dropped").value());
  stats.reports_failed_.inc();
  EXPECT_EQ(2, store.counter("tracing.datadog.reports_failed").value());
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
