#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/thread.h"
#include "source/common/stats/lazy_init.h"
#include "source/common/stats/thread_local_store.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace Test {

#define AWESOME_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME) COUNTER(foo)

MAKE_STAT_NAMES_STRUCT(AwesomeStatNames, AWESOME_STATS);
MAKE_STATS_STRUCT(AwesomeStats, AwesomeStatNames, AWESOME_STATS);

class LazyInitStatsTest : public testing::Test {
public:
  SymbolTableImpl symbol_table_;
  AllocatorImpl allocator_{symbol_table_};
  ThreadLocalStoreImpl store_{allocator_};
  AwesomeStatNames stats_names_{symbol_table_};
};

using LazyAwesomeStats = LazyCompatibleStats<AwesomeStats>;

TEST_F(LazyInitStatsTest, StatsGoneWithScope) {
  {
    ScopeSharedPtr scope = store_.createScope("bluh");
    LazyAwesomeStats x = LazyAwesomeStats::create(scope, stats_names_, true);
    LazyAwesomeStats y = LazyAwesomeStats::create(scope, stats_names_, true);
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
    x->foo_.inc();
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
    EXPECT_EQ(x->foo_.value(), 1);
    EXPECT_EQ(y->foo_.value(), 1);
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 2);
  }
  // Deleted as scope deleted.
  EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo"), nullptr);
  {
    ScopeSharedPtr scope = store_.createScope("bluh");
    LazyAwesomeStats x = LazyAwesomeStats::create(scope, stats_names_, true);
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
    EXPECT_EQ(x->foo_.value(), 0);
    // Initialized now.
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
  }
}

TEST_F(LazyInitStatsTest, StatsMutlipleInstancesDynamicallyDestructed) {
  {
    ScopeSharedPtr scope_1 = store_.createScope("bluh");
    auto x =
        std::make_unique<LazyAwesomeStats>(LazyAwesomeStats::create(scope_1, stats_names_, true));
    auto y =
        std::make_unique<LazyAwesomeStats>(LazyAwesomeStats::create(scope_1, stats_names_, true));
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
    (*x)->foo_.inc();
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
    EXPECT_EQ((*x)->foo_.value(), 1);
    EXPECT_EQ((*y)->foo_.value(), 1);
    // x,y instantiated.
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 2);
    // Only x instantiated.
    y.reset();
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
    EXPECT_EQ((*x)->foo_.value(), 1);
  }
  // Deleted as scope deleted.
  EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo"), nullptr);
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized"), nullptr);
  {
    ScopeSharedPtr scope_2 = store_.createScope("bluh");
    LazyAwesomeStats x = LazyAwesomeStats::create(scope_2, stats_names_, true);
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
    // Previous data is gone, as scope_2 and scope_1's lifecycle do not overlap.
    EXPECT_EQ(x->foo_.value(), 0);
    // Initialized now.
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
  }
}

TEST_F(LazyInitStatsTest, ScopeOutlivesLazyStats) {
      ScopeSharedPtr scope_1 = store_.createScope("bluh");
  {
    auto x =
        std::make_unique<LazyAwesomeStats>(LazyAwesomeStats::create(scope_1, stats_names_, true));
    auto y =
        std::make_unique<LazyAwesomeStats>(LazyAwesomeStats::create(scope_1, stats_names_, true));
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
    (*x)->foo_.inc();
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
    EXPECT_EQ((*x)->foo_.value(), 1);
    EXPECT_EQ((*y)->foo_.value(), 1);
    // x,y instantiated.
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 2);
    // Only x instantiated.
    y.reset();
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
    EXPECT_EQ((*x)->foo_.value(), 1);
  }
  // Both LazyAwesomeStats deleted.
  EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo")->value(), 1);
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);

  {
    // scope_1 overlaps with scope_2.
    ScopeSharedPtr scope_2 = store_.createScope("bluh");
    LazyAwesomeStats x = LazyAwesomeStats::create(scope_2, stats_names_, true);
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
    // Previous data is NOT gone, as scope_2 and scope_1's lifecycles overlap.
    EXPECT_EQ(x->foo_.value(), 1);
    // Initialized now.
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
  }
}

}
}
}
