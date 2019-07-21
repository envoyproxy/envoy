#include <string>

#include "common/stats/allocator_impl.h"
#include "common/stats/fake_symbol_table_impl.h"

#include "test/test_common/logging.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class AllocatorImplTest : public testing::Test {
protected:
  AllocatorImplTest() : alloc_(symbol_table_), pool_(symbol_table_) {}
  ~AllocatorImplTest() override { clearStorage(); }

  StatNameStorage makeStatStorage(absl::string_view name) {
    return StatNameStorage(name, symbol_table_);
  }

  StatName makeStat(absl::string_view name) { return pool_.add(name); }

  void clearStorage() {
    pool_.clear();
    EXPECT_EQ(0, symbol_table_.numSymbols());
  }

  FakeSymbolTableImpl symbol_table_;
  AllocatorImpl alloc_;
  StatNamePool pool_;
};

// Allocate 2 counters of the same name, and you'll get the same object.
TEST_F(AllocatorImplTest, CountersWithSameName) {
  StatName counter_name = makeStat("counter.name");
  CounterSharedPtr c1 = alloc_.makeCounter(counter_name, "", std::vector<Tag>());
  EXPECT_EQ(1, c1->use_count());
  CounterSharedPtr c2 = alloc_.makeCounter(counter_name, "", std::vector<Tag>());
  EXPECT_EQ(2, c1->use_count());
  EXPECT_EQ(2, c2->use_count());
  EXPECT_EQ(c1.get(), c2.get());
  EXPECT_FALSE(c1->used());
  EXPECT_FALSE(c2->used());
  c1->inc();
  EXPECT_TRUE(c1->used());
  EXPECT_TRUE(c2->used());
  c2->inc();
  EXPECT_EQ(2, c1->value());
  EXPECT_EQ(2, c2->value());
}

TEST_F(AllocatorImplTest, GaugesWithSameName) {
  StatName gauge_name = makeStat("gauges.name");
  GaugeSharedPtr g1 =
      alloc_.makeGauge(gauge_name, "", std::vector<Tag>(), Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1, g1->use_count());
  GaugeSharedPtr g2 =
      alloc_.makeGauge(gauge_name, "", std::vector<Tag>(), Gauge::ImportMode::Accumulate);
  EXPECT_EQ(2, g1->use_count());
  EXPECT_EQ(2, g2->use_count());
  EXPECT_EQ(g1.get(), g2.get());
  EXPECT_FALSE(g1->used());
  EXPECT_FALSE(g2->used());
  g1->inc();
  EXPECT_TRUE(g1->used());
  EXPECT_TRUE(g2->used());
  EXPECT_EQ(1, g1->value());
  EXPECT_EQ(1, g2->value());
  g2->dec();
  EXPECT_EQ(0, g1->value());
  EXPECT_EQ(0, g2->value());
}

} // namespace
} // namespace Stats
} // namespace Envoy
