#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/thread.h"
#include "source/common/stats/deferred_creation.h"
#include "source/common/stats/thread_local_store.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

#define AWESOME_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME) COUNTER(foo)

MAKE_STAT_NAMES_STRUCT(AwesomeStatNames, AWESOME_STATS);
MAKE_STATS_STRUCT(AwesomeStats, AwesomeStatNames, AWESOME_STATS);

class DeferredCreationStatsTest : public testing::Test {
public:
  SymbolTableImpl symbol_table_;
  AllocatorImpl allocator_{symbol_table_};
  ThreadLocalStoreImpl store_{allocator_};
  AwesomeStatNames stats_names_{symbol_table_};
};

using MyStats = DeferredCreationCompatibleStats<AwesomeStats>;

// Tests that non-lazy stats has no "AwesomeStats.initialized" gauge.
TEST_F(DeferredCreationStatsTest, NonLazyNoInitializedGauge) {
  {
    ScopeSharedPtr scope = store_.createScope("bluh");
    MyStats non_lazy_y = createDeferredCompatibleStats<AwesomeStats>(scope, stats_names_, false);
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized"), nullptr);
    EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo")->value(), 0);
    non_lazy_y->foo_.inc();
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized"), nullptr);
    EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo")->value(), 1);
  }
  // Scope gone, stats deleted.
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized"), nullptr);
  EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo"), nullptr);
}

// Tests that "AwesomeStats.initialized" gauge equals the number of initiated MyStats instances.
TEST_F(DeferredCreationStatsTest, StatsGoneWithScope) {
  {
    ScopeSharedPtr scope = store_.createScope("bluh");
    // No such gauge when there is no lazy init stats instances.
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized"), nullptr);
    MyStats x = createDeferredCompatibleStats<AwesomeStats>(scope, stats_names_, true);
    MyStats y = createDeferredCompatibleStats<AwesomeStats>(scope, stats_names_, true);
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
    EXPECT_FALSE(x.isPresent());
    x->foo_.inc();
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
    EXPECT_TRUE(x.isPresent());
    EXPECT_FALSE(y.isPresent());
    EXPECT_EQ(x->foo_.value(), 1);
    EXPECT_EQ(y->foo_.value(), 1);
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 2);
    EXPECT_TRUE(y.isPresent());
  }
  // Deleted as scope deleted.
  EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo"), nullptr);
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized"), nullptr);
  {
    // Recreate scope "bluh".
    ScopeSharedPtr scope = store_.createScope("bluh");
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized"), nullptr);
    MyStats x = createDeferredCompatibleStats<AwesomeStats>(scope, stats_names_, true);
    EXPECT_FALSE(x.isPresent());
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
    // Previous data is gone, as scope_v2 and scope_1's lifecycle do not overlap.
    EXPECT_EQ(x->foo_.value(), 0);
    // Initialized now.
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
    EXPECT_TRUE(x.isPresent());
  }
}

// Tests that multiple stats struct instances within the same scope has no issue to keep the
// stats, with removals.
TEST_F(DeferredCreationStatsTest, MultipleInstancesSameScopeDynamicallyDestructed) {
  {
    ScopeSharedPtr scope_1 = store_.createScope("bluh");
    auto x = std::make_unique<MyStats>(
        createDeferredCompatibleStats<AwesomeStats>(scope_1, stats_names_, true));
    auto y = std::make_unique<MyStats>(
        createDeferredCompatibleStats<AwesomeStats>(scope_1, stats_names_, true));
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
    // Only instantiate x, and then delete it.
    (*x)->foo_.inc();
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
    EXPECT_EQ((*x)->foo_.value(), 1);
    x.reset();
    // y is not instantiated before x was deleted, no AwesomeStats instance, but stats are not
    // lost.
    EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo")->value(), 1);
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
    // Instantiate y now.
    EXPECT_EQ((*y)->foo_.value(), 1);
    (*y)->foo_.inc();
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
    EXPECT_EQ((*y)->foo_.value(), 2);
  }
  // Deleted as scope deleted.
  EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo"), nullptr);
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized"), nullptr);
  {
    ScopeSharedPtr scope_v2 = store_.createScope("bluh");
    MyStats x = createDeferredCompatibleStats<AwesomeStats>(scope_v2, stats_names_, true);
    // Previous data is gone, as scope_v2 and scope_1's lifecycle do not overlap.
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
    EXPECT_EQ(x->foo_.value(), 0);
    // Initialized now.
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
  }
  // Deleted as scope deleted.
  EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo"), nullptr);
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized"), nullptr);
}

// Tests that as long as scope lives, stats under the scope won't be lost.
TEST_F(DeferredCreationStatsTest, ScopeOutlivesLazyStats) {
  ScopeSharedPtr scope_1 = store_.createScope("bluh");
  {
    auto x = std::make_unique<MyStats>(
        createDeferredCompatibleStats<AwesomeStats>(scope_1, stats_names_, true));
    auto y = std::make_unique<MyStats>(
        createDeferredCompatibleStats<AwesomeStats>(scope_1, stats_names_, true));
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
    (*x)->foo_.inc();
    EXPECT_EQ((*x)->foo_.value(), 2);
  }
  // Both MyStats deleted.
  EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo")->value(), 2);
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
  {
    // scope_1 overlaps with scope_v2.
    ScopeSharedPtr scope_v2 = store_.createScope("bluh");

    MyStats x_v2 = createDeferredCompatibleStats<AwesomeStats>(scope_v2, stats_names_, true);
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
    // Previous data is NOT gone, as scope_v2 and scope_1's lifecycle overlap.
    EXPECT_EQ(x_v2->foo_.value(), 2);

    x_v2->foo_.inc();
    EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo")->value(), 3);
    EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);
  }
  // scope_v2 is gone, but stat value kept since scope_1 is alive.
  EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo")->value(), 3);
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
}

// Tests that as two AwesomeStats instances of different scope, as long as the scope life-cycle
// overlaps, still got data kept when the earlier scope got deleted.
TEST_F(DeferredCreationStatsTest, WhenScopesOverlapStatsAreAliveAsLongAsThereAre) {

  ScopeSharedPtr scope_v1 = store_.createScope("bluh");
  auto x = std::make_unique<MyStats>(
      createDeferredCompatibleStats<AwesomeStats>(scope_v1, stats_names_, true));
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 0);
  (*x)->foo_.inc();
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);

  EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo")->value(), 1);
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);

  // Now scope_v2 gets created, but no action on any stats.
  ScopeSharedPtr scope_v2 = store_.createScope("bluh");
  auto y = std::make_unique<MyStats>(
      createDeferredCompatibleStats<AwesomeStats>(scope_v2, stats_names_, true));
  // NOTE: since x was instantiated, y is instantiated on creation.
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 2);

  // Now remove scope_v1, stats won't be lost.
  x.reset();
  scope_v1.reset();
  EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo")->value(), 1);
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized")->value(), 1);

  // remove scope_v2, stats will be gone.
  y.reset();
  scope_v2.reset();

  EXPECT_EQ(TestUtility::findCounter(store_, "bluh.foo"), nullptr);
  EXPECT_EQ(TestUtility::findGauge(store_, "bluh.AwesomeStats.initialized"), nullptr);
}

} // namespace
} // namespace Stats
} // namespace Envoy
