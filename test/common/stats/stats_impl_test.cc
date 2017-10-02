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
  t1.recordDuration(std::chrono::milliseconds(200));
  t2.recordDuration(std::chrono::milliseconds(200));

  Histogram& h1 = store.histogram("h1");
  Histogram& h2 = scope1->histogram("h2");
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  h1.recordValue(100);
  h2.recordValue(100);

  ScopePtr scope2 = scope1->createScope("foo.");
  EXPECT_EQ("scope1.foo.bar", scope2->counter("bar").name());

  EXPECT_EQ(3UL, store.counters().size());
  EXPECT_EQ(2UL, store.gauges().size());
}

} // namespace Stats
} // namespace Envoy
