#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/dynamic_modules/metric_registry.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace {

TEST(MetricRegistryTest, CounterByIdRecordsAndBoundsCheck) {
  Stats::IsolatedStoreImpl store;
  MetricRegistry registry(*store.rootScope());
  Stats::StatName name = registry.statNamePool().add("test_counter");
  Stats::Counter& counter = Stats::Utility::counterFromStatNames(registry.scope(), {name});
  const size_t id = registry.addCounter(MetricRegistry::CounterHandle(counter));
  EXPECT_EQ(id, 1);
  auto handle = registry.getCounterById(id);
  ASSERT_TRUE(handle.has_value());
  handle->add(5);
  EXPECT_EQ(counter.value(), 5);
  // The ABI uses 1-based ids, so 0 and out-of-range ids resolve to nothing.
  EXPECT_FALSE(registry.getCounterById(0).has_value());
  EXPECT_FALSE(registry.getCounterById(id + 1).has_value());
}

TEST(MetricRegistryTest, GaugeByIdRecords) {
  Stats::IsolatedStoreImpl store;
  MetricRegistry registry(*store.rootScope());
  Stats::StatName name = registry.statNamePool().add("test_gauge");
  Stats::Gauge& gauge = Stats::Utility::gaugeFromStatNames(registry.scope(), {name},
                                                           Stats::Gauge::ImportMode::Accumulate);
  const size_t id = registry.addGauge(MetricRegistry::GaugeHandle(gauge));
  auto handle = registry.getGaugeById(id);
  ASSERT_TRUE(handle.has_value());
  handle->increase(10);
  handle->decrease(3);
  handle->set(7);
  EXPECT_EQ(gauge.value(), 7);
}

TEST(MetricRegistryTest, HistogramByIdRecords) {
  Stats::IsolatedStoreImpl store;
  MetricRegistry registry(*store.rootScope());
  Stats::StatName name = registry.statNamePool().add("test_histogram");
  Stats::Histogram& hist = Stats::Utility::histogramFromStatNames(
      registry.scope(), {name}, Stats::Histogram::Unit::Unspecified);
  const size_t id = registry.addHistogram(MetricRegistry::HistogramHandle(hist));
  auto handle = registry.getHistogramById(id);
  ASSERT_TRUE(handle.has_value());
  handle->recordValue(42);
}

TEST(MetricRegistryTest, LabeledCounterRecordsWithTags) {
  Stats::IsolatedStoreImpl store;
  MetricRegistry registry(*store.rootScope());
  Stats::StatName name = registry.statNamePool().add("test_counter_vec");
  Stats::StatName label = registry.statNamePool().add("method");
  const size_t id = registry.addCounterVec(MetricRegistry::CounterVecHandle(name, {label}));
  auto handle = registry.getCounterVecById(id);
  ASSERT_TRUE(handle.has_value());
  EXPECT_EQ(handle->labelNames().size(), 1);
  Stats::StatNameDynamicPool dynamic_pool(store.symbolTable());
  Stats::StatNameTagVector tags{{label, dynamic_pool.add("GET")}};
  handle->add(registry.scope(), tags, 4);
  // The labeled counter is created lazily on first record through the element API.
  EXPECT_EQ(store.counters().size(), 1);
}

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
