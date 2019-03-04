#include <vector>

#include "common/stats/source_impl.h"

#include "test/mocks/stats/mocks.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnPointee;

namespace Envoy {
namespace Stats {

TEST(SourceImplTest, Caching) {
  NiceMock<MockStore> store;
  std::vector<CounterSharedPtr> stored_counters;
  std::vector<GaugeSharedPtr> stored_gauges;
  std::vector<ParentHistogramSharedPtr> stored_histograms;

  ON_CALL(store, counters()).WillByDefault(ReturnPointee(&stored_counters));
  ON_CALL(store, gauges()).WillByDefault(ReturnPointee(&stored_gauges));
  ON_CALL(store, histograms()).WillByDefault(ReturnPointee(&stored_histograms));

  SourceImpl source(store);

  // Once cached, new values should not be reflected by the return value.
  stored_counters.push_back(std::make_shared<MockCounter>());
  EXPECT_EQ(source.cachedCounters(), stored_counters);
  stored_counters.push_back(std::make_shared<MockCounter>());
  EXPECT_NE(source.cachedCounters(), stored_counters);

  stored_gauges.push_back(std::make_shared<MockGauge>());
  EXPECT_EQ(source.cachedGauges(), stored_gauges);
  stored_gauges.push_back(std::make_shared<MockGauge>());
  EXPECT_NE(source.cachedGauges(), stored_gauges);

  stored_histograms.push_back(std::make_shared<MockParentHistogram>());
  EXPECT_EQ(source.cachedHistograms(), stored_histograms);
  stored_histograms.push_back(std::make_shared<MockParentHistogram>());
  EXPECT_NE(source.cachedHistograms(), stored_histograms);

  // After clearing, the new values should be reflected in the cache.
  source.clearCache();
  EXPECT_EQ(source.cachedCounters(), stored_counters);
  EXPECT_EQ(source.cachedGauges(), stored_gauges);
  EXPECT_EQ(source.cachedHistograms(), stored_histograms);
}

} // namespace Stats
} // namespace Envoy
