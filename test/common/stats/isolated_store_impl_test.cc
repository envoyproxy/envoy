#include <string>

#include "envoy/stats/stats_macros.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/null_counter.h"
#include "source/common/stats/null_gauge.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class StatsIsolatedStoreImplTest : public testing::Test {
protected:
  StatsIsolatedStoreImplTest()
      : store_(std::make_unique<IsolatedStoreImpl>(symbol_table_)), pool_(symbol_table_),
        scope_(store_->rootScope()) {}
  ~StatsIsolatedStoreImplTest() override {
    pool_.clear();
    scope_.reset();
    store_.reset();
    EXPECT_EQ(0, symbol_table_.numSymbols());
  }

  StatName makeStatName(absl::string_view name) { return pool_.add(name); }

  SymbolTableImpl symbol_table_;
  std::unique_ptr<IsolatedStoreImpl> store_;
  StatNamePool pool_;
  ScopeSharedPtr scope_;
};

TEST_F(StatsIsolatedStoreImplTest, All) {
  EXPECT_TRUE(store_->fixedTags().empty());
  ScopeSharedPtr scope1 = scope_->createScope("scope1.");
  Counter& c1 = scope_->counterFromString("c1");
  Counter& c2 = scope1->counterFromString("c2");
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());
  EXPECT_EQ("c1", c1.tagExtractedName());
  EXPECT_EQ("scope1.c2", c2.tagExtractedName());
  EXPECT_EQ(0, c1.tags().size());
  EXPECT_EQ(0, c1.tags().size());
  CounterOptConstRef opt_counter = scope1->findCounter(c2.statName());
  ASSERT_TRUE(opt_counter);
  EXPECT_EQ(&c2, &opt_counter->get());
  StatName not_found = pool_.add("not_found");
  EXPECT_FALSE(scope1->findCounter(not_found));

  StatNameManagedStorage c1_name("c1", store_->symbolTable());
  c1.add(100);
  auto found_counter = scope_->findCounter(c1_name.statName());
  ASSERT_TRUE(found_counter.has_value());
  EXPECT_EQ(&c1, &found_counter->get());
  EXPECT_EQ(100, found_counter->get().value());
  c1.add(100);
  EXPECT_EQ(200, found_counter->get().value());

  Gauge& g1 = scope_->gaugeFromString("g1", Gauge::ImportMode::Accumulate);
  Gauge& g2 = scope1->gaugeFromString("g2", Gauge::ImportMode::Accumulate);
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());
  EXPECT_EQ("g1", g1.tagExtractedName());
  EXPECT_EQ("scope1.g2", g2.tagExtractedName());
  EXPECT_EQ(0, g1.tags().size());
  EXPECT_EQ(0, g2.tags().size());
  GaugeOptConstRef opt_gauge = scope1->findGauge(g2.statName());
  ASSERT_TRUE(opt_gauge);
  EXPECT_EQ(&g2, &opt_gauge->get());
  EXPECT_FALSE(scope1->findGauge(not_found));
  // TODO(jmarantz): There may be a bug with
  // scope1->findGauge(h1.statName()), which finds the histogram added to
  // the store, which is arguably not in the scope. Investigate what the
  // behavior should be.

  StatNameManagedStorage g1_name("g1", store_->symbolTable());
  g1.set(100);
  auto found_gauge = scope_->findGauge(g1_name.statName());
  ASSERT_TRUE(found_gauge.has_value());
  EXPECT_EQ(&g1, &found_gauge->get());
  EXPECT_EQ(100, found_gauge->get().value());
  g1.set(0);
  EXPECT_EQ(0, found_gauge->get().value());

  Histogram& h1 = scope_->histogramFromString("h1", Stats::Histogram::Unit::Unspecified);
  EXPECT_TRUE(h1.used()); // hardcoded in impl to be true always.
  EXPECT_TRUE(h1.use_count() == 1);
  Histogram& h2 = scope1->histogramFromString("h2", Stats::Histogram::Unit::Unspecified);
  store_->deliverHistogramToSinks(h2, 0);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  EXPECT_EQ("h1", h1.tagExtractedName());
  EXPECT_EQ("scope1.h2", h2.tagExtractedName());
  EXPECT_EQ(0, h1.tags().size());
  EXPECT_EQ(0, h2.tags().size());
  h1.recordValue(200);
  h2.recordValue(200);
  HistogramOptConstRef opt_histogram = scope1->findHistogram(h2.statName());
  ASSERT_TRUE(opt_histogram);
  EXPECT_EQ(&h2, &opt_histogram->get());
  EXPECT_FALSE(scope1->findHistogram(not_found));
  // TODO(jmarantz): There may be a bug with
  // scope1->findHistogram(h1.statName()), which finds the histogram added to
  // the store, which is arguably not in the scope. Investigate what the
  // behavior should be.

  StatNameManagedStorage h1_name("h1", store_->symbolTable());
  auto found_histogram = scope_->findHistogram(h1_name.statName());
  ASSERT_TRUE(found_histogram.has_value());
  EXPECT_EQ(&h1, &found_histogram->get());

  ScopeSharedPtr scope2 = scope1->scopeFromStatName(makeStatName("foo."));
  EXPECT_EQ("scope1.foo.bar", scope2->counterFromString("bar").name());

  // Validate that we sanitize away bad characters in the stats prefix.
  ScopeSharedPtr scope3 = scope1->createScope(std::string("foo:\0:.", 7));
  EXPECT_EQ("scope1.foo___.bar", scope3->counterFromString("bar").name());

  EXPECT_EQ(4UL, store_->counters().size());
  EXPECT_EQ(2UL, store_->gauges().size());

  StatNameManagedStorage nonexistent_name("nonexistent", store_->symbolTable());
  EXPECT_EQ(scope_->findCounter(nonexistent_name.statName()), absl::nullopt);
  EXPECT_EQ(scope_->findGauge(nonexistent_name.statName()), absl::nullopt);
  EXPECT_EQ(scope_->findHistogram(nonexistent_name.statName()), absl::nullopt);
}

TEST_F(StatsIsolatedStoreImplTest, PrefixIsStatName) {
  ScopeSharedPtr scope1 = scope_->createScope("scope1");
  ScopeSharedPtr scope2 = scope1->scopeFromStatName(makeStatName("scope2"));
  Counter& c1 = scope2->counterFromString("c1");
  EXPECT_EQ("scope1.scope2.c1", c1.name());
}

TEST_F(StatsIsolatedStoreImplTest, AllWithSymbolTable) {
  ScopeSharedPtr scope1 = scope_->createScope("scope1.");
  Counter& c1 = scope_->counterFromStatName(makeStatName("c1"));
  Counter& c2 = scope1->counterFromStatName(makeStatName("c2"));
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());
  EXPECT_EQ("c1", c1.tagExtractedName());
  EXPECT_EQ("scope1.c2", c2.tagExtractedName());
  EXPECT_EQ(0, c1.tags().size());
  EXPECT_EQ(0, c1.tags().size());

  Gauge& g1 = scope_->gaugeFromStatName(makeStatName("g1"), Gauge::ImportMode::Accumulate);
  Gauge& g2 = scope1->gaugeFromStatName(makeStatName("g2"), Gauge::ImportMode::Accumulate);
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());
  EXPECT_EQ("g1", g1.tagExtractedName());
  EXPECT_EQ("scope1.g2", g2.tagExtractedName());
  EXPECT_EQ(0, g1.tags().size());
  EXPECT_EQ(0, g2.tags().size());

  TextReadout& b1 = scope_->textReadoutFromStatName(makeStatName("b1"));
  TextReadout& b2 = scope1->textReadoutFromStatName(makeStatName("b2"));
  EXPECT_NE(&b1, &b2);
  EXPECT_EQ("b1", b1.name());
  EXPECT_EQ("scope1.b2", b2.name());
  EXPECT_EQ("b1", b1.tagExtractedName());
  EXPECT_EQ("scope1.b2", b2.tagExtractedName());
  EXPECT_EQ(0, b1.tags().size());
  EXPECT_EQ(0, b2.tags().size());
  Histogram& h1 =
      scope_->histogramFromStatName(makeStatName("h1"), Stats::Histogram::Unit::Unspecified);
  Histogram& h2 =
      scope1->histogramFromStatName(makeStatName("h2"), Stats::Histogram::Unit::Unspecified);
  store_->deliverHistogramToSinks(h2, 0);
  EXPECT_EQ("h1", h1.name());
  EXPECT_EQ("scope1.h2", h2.name());
  EXPECT_EQ("h1", h1.tagExtractedName());
  EXPECT_EQ("scope1.h2", h2.tagExtractedName());
  EXPECT_EQ(0, h1.tags().size());
  EXPECT_EQ(0, h2.tags().size());
  h1.recordValue(200);
  h2.recordValue(200);

  ScopeSharedPtr scope2 = scope1->createScope("foo.");
  EXPECT_EQ("scope1.foo.bar", scope2->counterFromStatName(makeStatName("bar")).name());

  // Validate that we sanitize away bad characters in the stats prefix.
  ScopeSharedPtr scope3 = scope1->createScope(std::string("foo:\0:.", 7));
  EXPECT_EQ("scope1.foo___.bar", scope3->counterFromString("bar").name());

  EXPECT_EQ(4UL, store_->counters().size());
  EXPECT_EQ(2UL, store_->gauges().size());
  EXPECT_EQ(2UL, store_->textReadouts().size());
}

TEST_F(StatsIsolatedStoreImplTest, CounterWithTag) {
  StatNameTagVector tags{{makeStatName("tag1"), makeStatName("tag1Value")}};
  StatNameTagVector tags2{{makeStatName("tag1"), makeStatName("tag1Value2")}};
  StatName base = makeStatName("counter");
  Counter& c1 = scope_->counterFromStatNameWithTags(base, tags);
  Counter& c2 = scope_->counterFromStatNameWithTags(base, tags2);
  EXPECT_EQ("counter.tag1.tag1Value", c1.name());
  EXPECT_EQ("counter", c1.tagExtractedName());
  EXPECT_THAT(c1.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value"}));
  EXPECT_EQ("counter.tag1.tag1Value2", c2.name());
  EXPECT_EQ("counter", c2.tagExtractedName());
  EXPECT_THAT(c2.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value2"}));
  // Verify that counterFromStatNameWithTags with the same params returns
  // the existing stat object.
  EXPECT_EQ(&c1, &scope_->counterFromStatNameWithTags(base, tags));
}

TEST_F(StatsIsolatedStoreImplTest, GaugeWithTags) {
  StatNameTagVector tags{{makeStatName("tag1"), makeStatName("tag1Value")},
                         {makeStatName("tag2"), makeStatName("tag2Value")}};
  // tags2 being a subset of tags to ensure no collision in that case.
  StatNameTagVector tags2{{makeStatName("tag2"), makeStatName("tag2Value")}};
  StatName base = makeStatName("gauge");
  Gauge& g1 = scope_->gaugeFromStatNameWithTags(base, tags, Gauge::ImportMode::Accumulate);
  Gauge& g2 = scope_->gaugeFromStatNameWithTags(base, tags2, Gauge::ImportMode::Accumulate);
  EXPECT_EQ("gauge.tag1.tag1Value.tag2.tag2Value", g1.name());
  EXPECT_EQ("gauge", g1.tagExtractedName());
  EXPECT_THAT(g1.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value"}, Tag{"tag2", "tag2Value"}));
  EXPECT_EQ("gauge.tag2.tag2Value", g2.name());
  EXPECT_EQ("gauge", g2.tagExtractedName());
  EXPECT_THAT(g2.tags(), testing::ElementsAre(Tag{"tag2", "tag2Value"}));
  // Verify that gaugeFromStatNameWithTags with the same params returns
  // the existing stat object.
  EXPECT_EQ(&g1, &scope_->gaugeFromStatNameWithTags(base, tags, Gauge::ImportMode::Accumulate));
}

TEST_F(StatsIsolatedStoreImplTest, TextReadoutWithTag) {
  StatNameTagVector tags{{makeStatName("tag1"), makeStatName("tag1Value")}};
  StatNameTagVector tags2{{makeStatName("tag1"), makeStatName("tag1Value2")}};
  StatName base = makeStatName("textreadout");
  TextReadout& b1 = scope_->textReadoutFromStatNameWithTags(base, tags);
  TextReadout& b2 = scope_->textReadoutFromStatNameWithTags(base, tags2);
  EXPECT_EQ("textreadout.tag1.tag1Value", b1.name());
  EXPECT_EQ("textreadout", b1.tagExtractedName());
  EXPECT_THAT(b1.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value"}));
  EXPECT_EQ("textreadout.tag1.tag1Value2", b2.name());
  EXPECT_EQ("textreadout", b2.tagExtractedName());
  EXPECT_THAT(b2.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value2"}));
  // Verify that textReadoutFromStatNameWithTags with the same params returns
  // the existing stat object.
  EXPECT_EQ(&b1, &scope_->textReadoutFromStatNameWithTags(base, tags));
}

TEST_F(StatsIsolatedStoreImplTest, HistogramWithTag) {
  StatNameTagVector tags{{makeStatName("tag1"), makeStatName("tag1Value")}};
  StatNameTagVector tags2{{makeStatName("tag1"), makeStatName("tag1Value2")}};
  StatName base = makeStatName("histogram");
  Histogram& h1 =
      scope_->histogramFromStatNameWithTags(base, tags, Stats::Histogram::Unit::Unspecified);
  Histogram& h2 =
      scope_->histogramFromStatNameWithTags(base, tags2, Stats::Histogram::Unit::Unspecified);
  EXPECT_EQ("histogram.tag1.tag1Value", h1.name());
  EXPECT_EQ("histogram", h1.tagExtractedName());
  EXPECT_THAT(h1.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value"}));
  EXPECT_EQ("histogram.tag1.tag1Value2", h2.name());
  EXPECT_EQ("histogram", h2.tagExtractedName());
  EXPECT_THAT(h2.tags(), testing::ElementsAre(Tag{"tag1", "tag1Value2"}));
  // Verify that histogramFromStatNameWithTags with the same params returns
  // the existing stat object.
  EXPECT_EQ(
      &h1, &scope_->histogramFromStatNameWithTags(base, tags, Stats::Histogram::Unit::Unspecified));
}

TEST_F(StatsIsolatedStoreImplTest, ConstSymtabAccessor) {
  ScopeSharedPtr scope = store_->createScope("scope.");
  const Scope& cscope = *scope;
  const SymbolTable& const_symbol_table = cscope.constSymbolTable();
  SymbolTable& symbol_table = scope->symbolTable();
  EXPECT_EQ(&const_symbol_table, &symbol_table);
}

TEST_F(StatsIsolatedStoreImplTest, LongStatName) {
  const std::string long_string(128, 'A');

  ScopeSharedPtr scope = store_->createScope("scope.");
  Counter& counter = scope->counterFromString(long_string);
  EXPECT_EQ(absl::StrCat("scope.", long_string), counter.name());
}

/**
 * Test stats macros. @see stats_macros.h
 */
#define ALL_TEST_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)                          \
  COUNTER(test_counter)                                                                            \
  GAUGE(test_gauge, Accumulate)                                                                    \
  HISTOGRAM(test_histogram, Microseconds)                                                          \
  TEXT_READOUT(test_text_readout)                                                                  \
  STATNAME(prefix)

struct TestStats {
  ALL_TEST_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT,
                 GENERATE_TEXT_READOUT_STRUCT, GENERATE_STATNAME_STRUCT)
};

TEST_F(StatsIsolatedStoreImplTest, StatsMacros) {
  TestStats test_stats{
      ALL_TEST_STATS(POOL_COUNTER_PREFIX(*store_, "test."), POOL_GAUGE_PREFIX(*store_, "test."),
                     POOL_HISTOGRAM_PREFIX(*store_, "test."),
                     POOL_TEXT_READOUT_PREFIX(*store_, "test."), GENERATE_STATNAME_STRUCT)};

  Counter& counter = test_stats.test_counter_;
  EXPECT_EQ("test.test_counter", counter.name());

  Gauge& gauge = test_stats.test_gauge_;
  EXPECT_EQ("test.test_gauge", gauge.name());

  TextReadout& textReadout = test_stats.test_text_readout_;
  EXPECT_EQ("test.test_text_readout", textReadout.name());

  Histogram& histogram = test_stats.test_histogram_;
  EXPECT_EQ("test.test_histogram", histogram.name());
  EXPECT_EQ(Histogram::Unit::Microseconds, histogram.unit());
}

TEST_F(StatsIsolatedStoreImplTest, NullImplCoverage) {
  NullCounterImpl& c = store_->nullCounter();
  c.inc();
  EXPECT_EQ(0, c.value());
  NullGaugeImpl& g = store_->nullGauge();
  g.inc();
  EXPECT_EQ(0, g.value());
}

TEST_F(StatsIsolatedStoreImplTest, StatNamesStruct) {
  MAKE_STAT_NAMES_STRUCT(StatNames, ALL_TEST_STATS);
  StatNames stat_names(store_->symbolTable());
  EXPECT_EQ("prefix", store_->symbolTable().toString(stat_names.prefix_));
  ScopeSharedPtr scope1 = store_->createScope("scope1.");
  ScopeSharedPtr scope2 = store_->createScope("scope2.");
  MAKE_STATS_STRUCT(Stats, StatNames, ALL_TEST_STATS);
  Stats stats1(stat_names, *scope1);
  EXPECT_EQ("scope1.test_counter", stats1.test_counter_.name());
  EXPECT_EQ("scope1.test_gauge", stats1.test_gauge_.name());
  EXPECT_EQ("scope1.test_histogram", stats1.test_histogram_.name());
  EXPECT_EQ("scope1.test_text_readout", stats1.test_text_readout_.name());
  Stats stats2(stat_names, *scope2, stat_names.prefix_);
  EXPECT_EQ("scope2.prefix.test_counter", stats2.test_counter_.name());
  EXPECT_EQ("scope2.prefix.test_gauge", stats2.test_gauge_.name());
  EXPECT_EQ("scope2.prefix.test_histogram", stats2.test_histogram_.name());
  EXPECT_EQ("scope2.prefix.test_text_readout", stats2.test_text_readout_.name());
}

TEST_F(StatsIsolatedStoreImplTest, SharedScopes) {
  std::vector<ConstScopeSharedPtr> scopes;

  // Verifies shared_ptr functionality by creating some scopes, iterating
  // through them from the store and saving them in a vector, dropping the
  // references, and then referencing the scopes, verifying their names.
  {
    ScopeSharedPtr scope1 = store_->createScope("scope1.");
    ScopeSharedPtr scope2 = store_->createScope("scope2.");
    store_->forEachScope(
        [](size_t) {}, [&scopes](const Scope& scope) { scopes.push_back(scope.getConstShared()); });
  }
  ASSERT_EQ(3, scopes.size());
  store_->symbolTable().sortByStatNames<ConstScopeSharedPtr>(
      scopes.begin(), scopes.end(),
      [](const ConstScopeSharedPtr& scope) -> StatName { return scope->prefix(); });
  EXPECT_EQ("", store_->symbolTable().toString(scopes[0]->prefix())); // default scope
  EXPECT_EQ("scope1", store_->symbolTable().toString(scopes[1]->prefix()));
  EXPECT_EQ("scope2", store_->symbolTable().toString(scopes[2]->prefix()));
}

} // namespace Stats
} // namespace Envoy
