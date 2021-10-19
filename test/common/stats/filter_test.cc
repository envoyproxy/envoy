#include <string>

#include "source/common/stats/filter.h"
#include "source/common/stats/isolated_store_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

namespace {

// Pick numeric suffixes for stat suffixes that will sort intuitively,
// e.g. c10, c11, c12. ... c20, c21, c22, ... c90, c91, c92
constexpr uint32_t FirstSuffix = 10;
constexpr uint32_t LastSuffix = 92;
constexpr uint32_t TotalStats = LastSuffix - FirstSuffix + 1;

constexpr uint32_t PageSize = 10;
constexpr uint32_t NumFullPages = TotalStats / PageSize; // truncation expected
constexpr uint32_t LastPageSize = TotalStats - PageSize * NumFullPages;

} // namespace

class FilterTest : public testing::Test {
protected:
  FilterTest() : store_(symbol_table_) {}

  std::string getStatName(absl::string_view prefix, uint32_t page, uint32_t index_in_page) {
    uint32_t index = index_in_page + page * PageSize + FirstSuffix;
    return absl::StrCat(prefix, index);
  }

  SymbolTableImpl symbol_table_;
  IsolatedStoreImpl store_;
};

TEST_F(FilterTest, Counters) {
  for (uint32_t i = FirstSuffix; i <= LastSuffix; ++i) {
    store_.counterFromString(absl::StrCat("c", i));
  }
  store_.counterFromString("c20").inc(); // Make one used counter.
  StatsFilter<Counter> counter_filter(store_);

  // Get the all the pages.
  StatName last;
  for (uint32_t page = 0; page < NumFullPages; ++page) {
    std::vector<CounterSharedPtr> counters = counter_filter.getFilteredStatsAfter(PageSize, last);
    ASSERT_EQ(PageSize, counters.size());
    for (uint32_t i = 0; i < PageSize; ++i) {
      EXPECT_EQ(getStatName("c", page, i), counters[i]->name());
    }
    last = counters[PageSize - 1]->statName();
  }

  // The last page will be empty.
  EXPECT_EQ(LastPageSize, counter_filter.getFilteredStatsAfter(PageSize, last).size());

  // Get the first page, including only used stats. There will be only one used stat.
  last = StatName();
  counter_filter.setUsedOnly(true);
  std::vector<CounterSharedPtr> counters = counter_filter.getFilteredStatsAfter(PageSize, last);
  ASSERT_EQ(1, counters.size());
  EXPECT_EQ("c20", counters[0]->name());
}

TEST_F(FilterTest, Gauges) {
  for (uint32_t i = FirstSuffix; i <= LastSuffix; ++i) {
    store_.gaugeFromString(absl::StrCat("g", i), Gauge::ImportMode::Accumulate);
  }
  store_.gaugeFromString("g20", Gauge::ImportMode::Accumulate).inc(); // Make one used gauge.
  StatsFilter<Gauge> gauge_filter(store_);

  // Get the all the pages.
  StatName last;
  for (uint32_t page = 0; page < NumFullPages; ++page) {
    std::vector<GaugeSharedPtr> gauges = gauge_filter.getFilteredStatsAfter(PageSize, last);
    ASSERT_EQ(PageSize, gauges.size());
    for (uint32_t i = 0; i < PageSize; ++i) {
      EXPECT_EQ(getStatName("g", page, i), gauges[i]->name());
    }
    last = gauges[PageSize - 1]->statName();
  }

  // The last page will be empty.
  EXPECT_EQ(LastPageSize, gauge_filter.getFilteredStatsAfter(PageSize, last).size());

  // Get the first page, including only used stats. There will be only one used stat.
  last = StatName();
  gauge_filter.setUsedOnly(true);
  std::vector<GaugeSharedPtr> gauges = gauge_filter.getFilteredStatsAfter(PageSize, last);
  ASSERT_EQ(1, gauges.size());
  EXPECT_EQ("g20", gauges[0]->name());
}

TEST_F(FilterTest, TextReadouts) {
  for (uint32_t i = FirstSuffix; i <= LastSuffix; ++i) {
    store_.textReadoutFromString(absl::StrCat("t", i));
  }
  store_.textReadoutFromString("t20").set("Hello!"); // Make one used text readout
  StatsFilter<TextReadout> text_readout_filter(store_);

  // Get the all the pages.
  StatName last;
  for (uint32_t page = 0; page < NumFullPages; ++page) {
    std::vector<TextReadoutSharedPtr> text_readouts =
        text_readout_filter.getFilteredStatsAfter(PageSize, last);
    ASSERT_EQ(PageSize, text_readouts.size());
    for (uint32_t i = 0; i < PageSize; ++i) {
      EXPECT_EQ(getStatName("t", page, i), text_readouts[i]->name());
    }
    last = text_readouts[PageSize - 1]->statName();
  }

  // The last page will be empty.
  EXPECT_EQ(LastPageSize, text_readout_filter.getFilteredStatsAfter(PageSize, last).size());

  // Get the first page, including only used stats. There will be only one used stat.
  last = StatName();
  text_readout_filter.setUsedOnly(true);
  std::vector<TextReadoutSharedPtr> text_readouts =
      text_readout_filter.getFilteredStatsAfter(PageSize, last);
  ASSERT_EQ(1, text_readouts.size());
  EXPECT_EQ("t20", text_readouts[0]->name());
}

} // namespace Stats
} // namespace Envoy
