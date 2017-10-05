#include <chrono>

#include "envoy/stats/stats_macros.h"

#include "common/stats/stats_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

TEST(StatsIsolatedStoreImplTest, All) {
  IsolatedStoreImpl store;

  ScopePtr scope1 = store.createScope("scope1.");
  Counter& c1 = store.counter("c1");
  Counter& c2 = scope1->counter("c2");
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());

  Gauge& g1 = store.gauge("g1");
  Gauge& g2 = scope1->gauge("g2");
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());

  Histogram& t1 = store.histogram(Histogram::ValueType::Duration, "t1");
  Histogram& t2 = scope1->histogram(Histogram::ValueType::Duration, "t2");
  EXPECT_EQ("t1", t1.name());
  EXPECT_EQ("scope1.t2", t2.name());
  EXPECT_EQ(Histogram::ValueType::Duration, t1.type());
  EXPECT_EQ(Histogram::ValueType::Duration, t2.type());
  t1.recordValue(200);
  t2.recordValue(200);

  Histogram& h1 = store.histogram(Histogram::ValueType::Integer, "h1");
  Histogram& h2 = scope1->histogram(Histogram::ValueType::Integer, "h2");
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  EXPECT_EQ(Histogram::ValueType::Integer, h1.type());
  EXPECT_EQ(Histogram::ValueType::Integer, h2.type());
  h1.recordValue(100);
  h2.recordValue(100);

  EXPECT_THROW_WITH_MESSAGE(
      store.histogram(Histogram::ValueType::Duration, "h1"), EnvoyException,
      "Cached histogram type did not match the requested type for name: 'h1'");

  ScopePtr scope2 = scope1->createScope("foo.");
  EXPECT_EQ("scope1.foo.bar", scope2->counter("bar").name());

  EXPECT_EQ(3UL, store.counters().size());
  EXPECT_EQ(2UL, store.gauges().size());
}

/**
 * Test stats macros. @see stats_macros.h
 */
// clang-format off
#define ALL_TEST_STATS(COUNTER, GAUGE, TIMER, HISTOGRAM)                                           \
  COUNTER  (test_counter)                                                                          \
  GAUGE    (test_gauge)                                                                            \
  TIMER    (test_timer)                                                                            \
  HISTOGRAM(test_histogram)
// clang-format on

struct TestStats {
  ALL_TEST_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_TIMER_STRUCT,
                 GENERATE_HISTOGRAM_STRUCT)
};

TEST(StatsMacros, All) {
  IsolatedStoreImpl stats_store;
  TestStats test_stats{ALL_TEST_STATS(
      POOL_COUNTER_PREFIX(stats_store, "test."), POOL_GAUGE_PREFIX(stats_store, "test."),
      POOL_TIMER_PREFIX(stats_store, "test."), POOL_HISTOGRAM_PREFIX(stats_store, "test."))};

  Counter& counter = test_stats.test_counter_;
  EXPECT_EQ("test.test_counter", counter.name());

  Gauge& gauge = test_stats.test_gauge_;
  EXPECT_EQ("test.test_gauge", gauge.name());

  Histogram& timer = test_stats.test_timer_;
  EXPECT_EQ("test.test_timer", timer.name());
  EXPECT_EQ(Histogram::ValueType::Duration, timer.type());

  Histogram& histogram = test_stats.test_histogram_;
  EXPECT_EQ("test.test_histogram", histogram.name());
  EXPECT_EQ(Histogram::ValueType::Integer, histogram.type());
}

} // namespace Stats
} // namespace Envoy
