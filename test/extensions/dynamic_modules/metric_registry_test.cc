#include <array>

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
  EXPECT_FALSE(registry.getGaugeById(0).has_value());
  EXPECT_FALSE(registry.getGaugeById(id + 1).has_value());
}

TEST(MetricRegistryTest, HistogramByIdRecords) {
  Stats::IsolatedStoreImpl store;
  MetricRegistry registry(*store.rootScope());
  Stats::StatName name = registry.statNamePool().add("test_histogram");
  Stats::Histogram& histogram = Stats::Utility::histogramFromStatNames(
      registry.scope(), {name}, Stats::Histogram::Unit::Unspecified);
  const size_t id = registry.addHistogram(MetricRegistry::HistogramHandle(histogram));
  auto handle = registry.getHistogramById(id);
  ASSERT_TRUE(handle.has_value());
  handle->recordValue(42);
  EXPECT_FALSE(registry.getHistogramById(0).has_value());
  EXPECT_FALSE(registry.getHistogramById(id + 1).has_value());
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
  EXPECT_FALSE(registry.getCounterVecById(0).has_value());
  EXPECT_FALSE(registry.getCounterVecById(id + 1).has_value());
}

TEST(MetricRegistryTest, LabeledGaugeRecordsWithTags) {
  Stats::IsolatedStoreImpl store;
  MetricRegistry registry(*store.rootScope());
  Stats::StatName name = registry.statNamePool().add("test_gauge_vec");
  Stats::StatName label = registry.statNamePool().add("state");
  const size_t id = registry.addGaugeVec(
      MetricRegistry::GaugeVecHandle(name, {label}, Stats::Gauge::ImportMode::Accumulate));
  auto handle = registry.getGaugeVecById(id);
  ASSERT_TRUE(handle.has_value());
  EXPECT_EQ(handle->labelNames().size(), 1);
  Stats::StatNameDynamicPool dynamic_pool(store.symbolTable());
  Stats::StatNameTagVector tags{{label, dynamic_pool.add("open")}};
  handle->increase(registry.scope(), tags, 10);
  handle->decrease(registry.scope(), tags, 3);
  handle->set(registry.scope(), tags, 7);
  EXPECT_EQ(store.gauges().size(), 1);
  // The ABI uses 1-based ids, so 0 and out-of-range ids resolve to nothing.
  EXPECT_FALSE(registry.getGaugeVecById(0).has_value());
  EXPECT_FALSE(registry.getGaugeVecById(id + 1).has_value());
}

TEST(MetricRegistryTest, LabeledHistogramRecordsWithTags) {
  Stats::IsolatedStoreImpl store;
  MetricRegistry registry(*store.rootScope());
  Stats::StatName name = registry.statNamePool().add("test_histogram_vec");
  Stats::StatName label = registry.statNamePool().add("result");
  const size_t id = registry.addHistogramVec(
      MetricRegistry::HistogramVecHandle(name, {label}, Stats::Histogram::Unit::Milliseconds));
  auto handle = registry.getHistogramVecById(id);
  ASSERT_TRUE(handle.has_value());
  EXPECT_EQ(handle->labelNames().size(), 1);
  Stats::StatNameDynamicPool dynamic_pool(store.symbolTable());
  Stats::StatNameTagVector tags{{label, dynamic_pool.add("ok")}};
  handle->recordValue(registry.scope(), tags, 42);
  // The ABI uses 1-based ids, so 0 and out-of-range ids resolve to nothing.
  EXPECT_FALSE(registry.getHistogramVecById(0).has_value());
  EXPECT_FALSE(registry.getHistogramVecById(id + 1).has_value());
}

TEST(MetricRegistryTest, ResolveCounterVecReturnsStableChild) {
  Stats::IsolatedStoreImpl store;
  MetricRegistry registry(*store.rootScope());
  Stats::StatName name = registry.statNamePool().add("resolve_counter_vec");
  Stats::StatName label = registry.statNamePool().add("method");
  const size_t id = registry.addCounterVec(MetricRegistry::CounterVecHandle(name, {label}));
  auto handle = registry.getCounterVecById(id);
  ASSERT_TRUE(handle.has_value());
  Stats::StatNameDynamicPool dynamic_pool(store.symbolTable());
  Stats::StatNameTagVector tags{{label, dynamic_pool.add("GET")}};
  Stats::Counter& counter = handle->resolve(registry.scope(), tags);
  counter.add(3);
  // Resolving the same tuple returns the same child, so records accumulate on one counter.
  EXPECT_EQ(&counter, &handle->resolve(registry.scope(), tags));
  EXPECT_EQ(counter.value(), 3);
  EXPECT_EQ(store.counters().size(), 1);
}

TEST(MetricRegistryTest, ResolveGaugeVecReturnsStableChild) {
  Stats::IsolatedStoreImpl store;
  MetricRegistry registry(*store.rootScope());
  Stats::StatName name = registry.statNamePool().add("resolve_gauge_vec");
  Stats::StatName label = registry.statNamePool().add("state");
  const size_t id = registry.addGaugeVec(
      MetricRegistry::GaugeVecHandle(name, {label}, Stats::Gauge::ImportMode::Accumulate));
  auto handle = registry.getGaugeVecById(id);
  ASSERT_TRUE(handle.has_value());
  Stats::StatNameDynamicPool dynamic_pool(store.symbolTable());
  Stats::StatNameTagVector tags{{label, dynamic_pool.add("open")}};
  Stats::Gauge& gauge = handle->resolve(registry.scope(), tags);
  gauge.set(9);
  EXPECT_EQ(&gauge, &handle->resolve(registry.scope(), tags));
  EXPECT_EQ(gauge.value(), 9);
}

TEST(MetricRegistryTest, ResolveHistogramVecReturnsStableChild) {
  Stats::IsolatedStoreImpl store;
  MetricRegistry registry(*store.rootScope());
  Stats::StatName name = registry.statNamePool().add("resolve_histogram_vec");
  Stats::StatName label = registry.statNamePool().add("result");
  const size_t id = registry.addHistogramVec(
      MetricRegistry::HistogramVecHandle(name, {label}, Stats::Histogram::Unit::Milliseconds));
  auto handle = registry.getHistogramVecById(id);
  ASSERT_TRUE(handle.has_value());
  Stats::StatNameDynamicPool dynamic_pool(store.symbolTable());
  Stats::StatNameTagVector tags{{label, dynamic_pool.add("ok")}};
  Stats::Histogram& histogram = handle->resolve(registry.scope(), tags);
  histogram.recordValue(42);
  EXPECT_EQ(&histogram, &handle->resolve(registry.scope(), tags));
}

TEST(MetricRegistryTest, IdResolvesOnlyItsOwnMetric) {
  Stats::IsolatedStoreImpl store;
  MetricRegistry registry(*store.rootScope());
  Stats::StatNamePool& pool = registry.statNamePool();
  Stats::StatName label = pool.add("label");
  const size_t counter_id = registry.addCounter(MetricRegistry::CounterHandle(
      Stats::Utility::counterFromStatNames(registry.scope(), {pool.add("counter")})));
  const size_t counter_vec_id =
      registry.addCounterVec(MetricRegistry::CounterVecHandle(pool.add("counter_vec"), {label}));
  const size_t gauge_id =
      registry.addGauge(MetricRegistry::GaugeHandle(Stats::Utility::gaugeFromStatNames(
          registry.scope(), {pool.add("gauge")}, Stats::Gauge::ImportMode::Accumulate)));
  const size_t gauge_vec_id = registry.addGaugeVec(MetricRegistry::GaugeVecHandle(
      pool.add("gauge_vec"), {label}, Stats::Gauge::ImportMode::Accumulate));
  const size_t histogram_id =
      registry.addHistogram(MetricRegistry::HistogramHandle(Stats::Utility::histogramFromStatNames(
          registry.scope(), {pool.add("histogram")}, Stats::Histogram::Unit::Unspecified)));
  const size_t histogram_vec_id = registry.addHistogramVec(MetricRegistry::HistogramVecHandle(
      pool.add("histogram_vec"), {label}, Stats::Histogram::Unit::Unspecified));

  // Each id resolves only through the getter of its own metric, whatever the other metrics are.
  const std::array<size_t, 6> ids = {counter_id,   counter_vec_id, gauge_id,
                                     gauge_vec_id, histogram_id,   histogram_vec_id};
  for (size_t i = 0; i < ids.size(); ++i) {
    EXPECT_EQ(i == 0, registry.getCounterById(ids[i]).has_value());
    EXPECT_EQ(i == 1, registry.getCounterVecById(ids[i]).has_value());
    EXPECT_EQ(i == 2, registry.getGaugeById(ids[i]).has_value());
    EXPECT_EQ(i == 3, registry.getGaugeVecById(ids[i]).has_value());
    EXPECT_EQ(i == 4, registry.getHistogramById(ids[i]).has_value());
    EXPECT_EQ(i == 5, registry.getHistogramVecById(ids[i]).has_value());
  }
}

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
