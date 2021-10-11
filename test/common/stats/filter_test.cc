#include "source/common/stats/filter.h"

#include <string>

#include "source/common/stats/isolated_store_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class FilterTest : public testing::Test {
protected:
  FilterTest()
      : store_(std::make_unique<IsolatedStoreImpl>(symbol_table_)), pool_(symbol_table_) {}
  ~FilterTest() override {
    pool_.clear();
    store_.reset();
  }

  StatName makeStatName(absl::string_view name) { return pool_.add(name); }

  // Adds 90 counters c10 through c99.
  void add90Counters() {
    for (uint32_t i = 10; i <= 99; ++i) {
      counters_.push_back(std::ref(store_->counterFromString(absl::StrCat("c", i))));
    }
  }

  SymbolTableImpl symbol_table_;
  std::unique_ptr<IsolatedStoreImpl> store_;
  StatNamePool pool_;
  std::vector<std::reference_wrapper<Counter>> counters_;
};

TEST_F(FilterTest, CountersNoFilter) {
  add90Counters();
  counters_[10].get().inc();  // Make only "c20" used.
  StatsFilter<Counter> counter_filter(*store_);
  std::vector<CounterSharedPtr> counters;

  // Get the first page, including all stats.
  StatName last;
  counters = counter_filter.getFilteredStatsAfter(10, last);
  ASSERT_EQ(10, counters.size());
  for (uint32_t i = 0; i < 10; ++i) {
    EXPECT_EQ(counters_[i].get().name(), counters[i]->name());
  }

  // Get the second page, including all stats.
  last = counters[9]->statName();
  counters = counter_filter.getFilteredStatsAfter(10, last);
  ASSERT_EQ(10, counters.size());
  for (uint32_t i = 0; i < 10; ++i) {
    EXPECT_EQ(counters_[i + 10].get().name(), counters[i]->name());
  }

  // Get the first page, including only used stats.
  last = StatName();
  counter_filter.setUsedOnly(true);
  counters = counter_filter.getFilteredStatsAfter(10, last);
  ASSERT_EQ(1, counters.size());
  EXPECT_EQ("c20", counters[0]->name());
}

} // namespace Stats
} // namespace Envoy
