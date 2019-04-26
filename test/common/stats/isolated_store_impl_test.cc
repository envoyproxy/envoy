#include <string>

#include "envoy/stats/stats_macros.h"

#include "common/stats/isolated_store_impl.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class StatsIsolatedStoreImplTest : public testing::Test {
protected:
  ~StatsIsolatedStoreImplTest() override { clearStorage(); }

  void clearStorage() {
    for (auto& stat_name_storage : stat_name_storage_) {
      stat_name_storage.free(store_.symbolTable());
    }
    stat_name_storage_.clear();
    EXPECT_EQ(0, store_.symbolTable().numSymbols());
  }

  StatName makeStatName(absl::string_view name) {
    stat_name_storage_.emplace_back(makeStatStorage(name));
    return stat_name_storage_.back().statName();
  }

  StatNameStorage makeStatStorage(absl::string_view name) {
    return StatNameStorage(name, store_.symbolTable());
  }

  IsolatedStoreImpl store_;
  std::vector<StatNameStorage> stat_name_storage_;
};

TEST_F(StatsIsolatedStoreImplTest, All) {
  ScopePtr scope1 = store_.createScope("scope1.");
  Counter& c1 = store_.counter("c1");
  Counter& c2 = scope1->counter("c2");
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());
  EXPECT_EQ("c1", c1.tagExtractedName());
  EXPECT_EQ("scope1.c2", c2.tagExtractedName());
  EXPECT_EQ(0, c1.tags().size());
  EXPECT_EQ(0, c1.tags().size());

  Gauge& g1 = store_.gauge("g1");
  Gauge& g2 = scope1->gauge("g2");
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());
  EXPECT_EQ("g1", g1.tagExtractedName());
  EXPECT_EQ("scope1.g2", g2.tagExtractedName());
  EXPECT_EQ(0, g1.tags().size());
  EXPECT_EQ(0, g2.tags().size());

  Histogram& h1 = store_.histogram("h1");
  Histogram& h2 = scope1->histogram("h2");
  scope1->deliverHistogramToSinks(h2, 0);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  EXPECT_EQ("h1", h1.tagExtractedName());
  EXPECT_EQ("scope1.h2", h2.tagExtractedName());
  EXPECT_EQ(0, h1.tags().size());
  EXPECT_EQ(0, h2.tags().size());
  h1.recordValue(200);
  h2.recordValue(200);

  ScopePtr scope2 = scope1->createScope("foo.");
  EXPECT_EQ("scope1.foo.bar", scope2->counter("bar").name());

  // Validate that we sanitize away bad characters in the stats prefix.
  ScopePtr scope3 = scope1->createScope(std::string("foo:\0:.", 7));
  EXPECT_EQ("scope1.foo___.bar", scope3->counter("bar").name());

  EXPECT_EQ(4UL, store_.counters().size());
  EXPECT_EQ(2UL, store_.gauges().size());
}

TEST_F(StatsIsolatedStoreImplTest, AllWithSymbolTable) {
  ScopePtr scope1 = store_.createScope("scope1.");
  Counter& c1 = store_.counterFromStatName(makeStatName("c1"));
  Counter& c2 = scope1->counterFromStatName(makeStatName("c2"));
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());
  EXPECT_EQ("c1", c1.tagExtractedName());
  EXPECT_EQ("scope1.c2", c2.tagExtractedName());
  EXPECT_EQ(0, c1.tags().size());
  EXPECT_EQ(0, c1.tags().size());

  Gauge& g1 = store_.gaugeFromStatName(makeStatName("g1"));
  Gauge& g2 = scope1->gaugeFromStatName(makeStatName("g2"));
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());
  EXPECT_EQ("g1", g1.tagExtractedName());
  EXPECT_EQ("scope1.g2", g2.tagExtractedName());
  EXPECT_EQ(0, g1.tags().size());
  EXPECT_EQ(0, g1.tags().size());

  Histogram& h1 = store_.histogramFromStatName(makeStatName("h1"));
  Histogram& h2 = scope1->histogramFromStatName(makeStatName("h2"));
  scope1->deliverHistogramToSinks(h2, 0);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  EXPECT_EQ("h1", h1.tagExtractedName());
  EXPECT_EQ("scope1.h2", h2.tagExtractedName());
  EXPECT_EQ(0, h1.tags().size());
  EXPECT_EQ(0, h2.tags().size());
  h1.recordValue(200);
  h2.recordValue(200);

  ScopePtr scope2 = scope1->createScope("foo.");
  EXPECT_EQ("scope1.foo.bar", scope2->counterFromStatName(makeStatName("bar")).name());

  // Validate that we sanitize away bad characters in the stats prefix.
  ScopePtr scope3 = scope1->createScope(std::string("foo:\0:.", 7));
  EXPECT_EQ("scope1.foo___.bar", scope3->counter("bar").name());

  EXPECT_EQ(4UL, store_.counters().size());
  EXPECT_EQ(2UL, store_.gauges().size());
}

TEST_F(StatsIsolatedStoreImplTest, LongStatName) {
  const std::string long_string(128, 'A');

  ScopePtr scope = store_.createScope("scope.");
  Counter& counter = scope->counter(long_string);
  EXPECT_EQ(absl::StrCat("scope.", long_string), counter.name());
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

TEST_F(StatsIsolatedStoreImplTest, StatsMacros) {
  TestStats test_stats{ALL_TEST_STATS(POOL_COUNTER_PREFIX(store_, "test."),
                                      POOL_GAUGE_PREFIX(store_, "test."),
                                      POOL_HISTOGRAM_PREFIX(store_, "test."))};

  Counter& counter = test_stats.test_counter_;
  EXPECT_EQ("test.test_counter", counter.name());

  Gauge& gauge = test_stats.test_gauge_;
  EXPECT_EQ("test.test_gauge", gauge.name());

  Histogram& histogram = test_stats.test_histogram_;
  EXPECT_EQ("test.test_histogram", histogram.name());
}

} // namespace Stats
} // namespace Envoy
