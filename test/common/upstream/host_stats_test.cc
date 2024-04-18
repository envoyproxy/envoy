#include "envoy/upstream/host_description.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

// Verify that counters are sorted by name.
TEST(HostStatsTest, CountersSortedByName) {
  HostStats host_stats;
  std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>> counters =
      host_stats.counters();
  EXPECT_FALSE(counters.empty());

  for (size_t i = 1; i < counters.size(); ++i) {
    EXPECT_LT(counters[i - 1].first, counters[i].first);
  }
}

// Verify that gauges are sorted by name.
TEST(HostStatsTest, GaugesSortedByName) {
  HostStats host_stats;
  std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>> gauges =
      host_stats.gauges();
  EXPECT_FALSE(gauges.empty());

  for (size_t i = 1; i < gauges.size(); ++i) {
    EXPECT_LT(gauges[i - 1].first, gauges[i].first);
  }
}

} // namespace
} // namespace Upstream
} // namespace Envoy
