#include <chrono>

#include "common/stats/stats_impl.h"

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

  Timer& t1 = store.timer("t1");
  Timer& t2 = scope1->timer("t2");
  EXPECT_EQ("t1", t1.name());
  EXPECT_EQ("scope1.t2", t2.name());

  store.deliverHistogramToSinks("h", 100);
  store.deliverTimingToSinks("t", std::chrono::milliseconds(200));
  scope1->deliverHistogramToSinks("h", 100);
  scope1->deliverTimingToSinks("t", std::chrono::milliseconds(200));

  EXPECT_EQ(2UL, store.counters().size());
  EXPECT_EQ(2UL, store.gauges().size());
}

} // namespace Stats
} // namespace Envoy
