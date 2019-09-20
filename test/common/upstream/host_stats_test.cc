#include "envoy/upstream/host_description.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

// Verify that counters are sorted by name.
TEST(HostStatsTest, CountersSortedByName) {
  HostStats host_stats;
  auto counters = host_stats.counters();
  EXPECT_FALSE(counters.empty());

  const absl::string_view* prev_counter_name = nullptr;
  for (const auto& counter : counters) {
    if (prev_counter_name) {
      EXPECT_LT(*prev_counter_name, counter.first);
    }
    prev_counter_name = &counter.first;
  }
}

// Verify that gauges are sorted by name.
TEST(HostStatsTest, GaugesSortedByName) {
  HostStats host_stats;
  auto gauges = host_stats.gauges();
  EXPECT_FALSE(gauges.empty());

  const absl::string_view* prev_gauge_name = nullptr;
  for (const auto& gauge : gauges) {
    if (prev_gauge_name) {
      EXPECT_LT(*prev_gauge_name, gauge.first);
    }
    prev_gauge_name = &gauge.first;
  }
}

} // namespace
} // namespace Upstream
} // namespace Envoy
