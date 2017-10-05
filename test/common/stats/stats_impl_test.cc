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

  Histogram& h1 = store.histogram("h1");
  Histogram& h2 = scope1->histogram("h2");
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  h1.recordValue(200);
  h2.recordValue(200);

  ScopePtr scope2 = scope1->createScope("foo.");
  EXPECT_EQ("scope1.foo.bar", scope2->counter("bar").name());

  EXPECT_EQ(3UL, store.counters().size());
  EXPECT_EQ(2UL, store.gauges().size());
}

/**
 * Test stats macros. @see stats_macros.h
 */
// clang-format off
#define ALL_TEST_STATS(COUNTER, GAUGE, HISTOGRAM)                                                  \
  COUNTER  (test_counter)                                                                          \
  GAUGE    (test_gauge)                                                                            \
  HISTOGRAM(test_histogram)
// clang-format on

struct TestStats {
  ALL_TEST_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

TEST(StatsMacros, All) {
  IsolatedStoreImpl stats_store;
  TestStats test_stats{ALL_TEST_STATS(POOL_COUNTER_PREFIX(stats_store, "test."),
                                      POOL_GAUGE_PREFIX(stats_store, "test."),
                                      POOL_HISTOGRAM_PREFIX(stats_store, "test."))};

  Counter& counter = test_stats.test_counter_;
  EXPECT_EQ("test.test_counter", counter.name());

  Gauge& gauge = test_stats.test_gauge_;
  EXPECT_EQ("test.test_gauge", gauge.name());

  Histogram& histogram = test_stats.test_histogram_;
  EXPECT_EQ("test.test_histogram", histogram.name());
}

} // namespace Stats
} // namespace Envoy
