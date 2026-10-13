#include <string>
#include <vector>

#include "envoy/stats/stats_macros.h"
#include "envoy/stats/tag.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/null_counter.h"
#include "source/common/stats/null_gauge.h"
#include "source/common/stats/thread_local_store.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::UnorderedElementsAre;

namespace Envoy {
namespace Stats {
namespace {

// All the tests should be run for both IsolatedStore and ThreadLocalStore.
enum class StoreType {
  ThreadLocal,
  Isolated,
};

class StatsUtilityTest : public testing::TestWithParam<StoreType> {
protected:
  template <class StatType>
  using IterateFn = std::function<bool(const RefcountPtr<StatType>& stat)>;
  using MakeStatFn = std::function<void(Scope& scope, const ElementVec& elements)>;

  StatsUtilityTest()
      : symbol_table_(std::make_unique<SymbolTableImpl>()), pool_(*symbol_table_),
        tags_(
            {{pool_.add("tag1"), pool_.add("value1")}, {pool_.add("tag2"), pool_.add("value2")}}) {
    switch (GetParam()) {
    case StoreType::ThreadLocal:
      alloc_ = std::make_unique<Allocator>(*symbol_table_),
      store_ = std::make_unique<ThreadLocalStoreImpl>(*alloc_);
      break;
    case StoreType::Isolated:
      store_ = std::make_unique<IsolatedStoreImpl>(*symbol_table_);
      break;
    }
    scope_ = store_->createScope("scope");
  }

  ~StatsUtilityTest() override {
    scope_.reset();
    pool_.clear();
    store_.reset();
    EXPECT_EQ(0, symbol_table_->numSymbols());
  }

  void init(MakeStatFn make_stat) {
    make_stat(*store_->rootScope(), {pool_.add("symbolic1")});
    make_stat(*store_->rootScope(), {Stats::DynamicName("dynamic1")});
    make_stat(*scope_, {pool_.add("symbolic2")});
    make_stat(*scope_, {Stats::DynamicName("dynamic2")});
    make_stat(*scope_, {Stats::DynamicSavedName("dynamicsaved3")});
  }

  template <class StatType> IterateFn<StatType> iterOnce() {
    return [this](const RefcountPtr<StatType>& stat) -> bool {
      results_.insert(stat->name());
      return false;
    };
  }

  template <class StatType> IterateFn<StatType> iterAll() {
    return [this](const RefcountPtr<StatType>& stat) -> bool {
      results_.insert(stat->name());
      return true;
    };
  }

  static MakeStatFn makeCounter() {
    return [](Scope& scope, const ElementVec& elements) {
      Utility::counterFromElements(scope, elements).inc();
    };
  }

  static bool checkValue(const Counter& counter) { return counter.value() == 1; }

  static MakeStatFn makeGauge() {
    return [](Scope& scope, const ElementVec& elements) {
      Utility::gaugeFromElements(scope, elements, Gauge::ImportMode::Accumulate).inc();
    };
  }

  static bool checkValue(const Gauge& gauge) { return gauge.value() == 1; }

  static MakeStatFn makeHistogram() {
    return [](Scope& scope, const ElementVec& elements) {
      Utility::histogramFromElements(scope, elements, Histogram::Unit::Milliseconds);
    };
  }

  static bool checkValue(const Histogram& histogram) {
    return histogram.unit() == Histogram::Unit::Milliseconds;
  }

  static MakeStatFn makeTextReadout() {
    return [](Scope& scope, const ElementVec& elements) {
      Utility::textReadoutFromElements(scope, elements).set("my-value");
    };
  }

  static bool checkValue(const TextReadout& text_readout) {
    return text_readout.value() == "my-value";
  }

  template <class StatType> void storeOnce(const MakeStatFn make_stat) {
    CachedReference<StatType> symbolic1_ref(*store_->rootScope(), "symbolic1");
    CachedReference<StatType> dynamic1_ref(*store_->rootScope(), "dynamic1");
    EXPECT_FALSE(symbolic1_ref.get());
    EXPECT_FALSE(dynamic1_ref.get());

    init(make_stat);

    ASSERT_TRUE(symbolic1_ref.get());
    ASSERT_TRUE(dynamic1_ref.get());
    EXPECT_FALSE(store_->iterate(iterOnce<StatType>()));
    EXPECT_EQ(1, results_.size());
    EXPECT_TRUE(checkValue(*symbolic1_ref.get()));
    EXPECT_TRUE(checkValue(*dynamic1_ref.get()));
  }

  template <class StatType> void storeAll(const MakeStatFn make_stat) {
    init(make_stat);
    EXPECT_TRUE(store_->iterate(iterAll<StatType>()));
    EXPECT_THAT(results_, UnorderedElementsAre("symbolic1", "dynamic1", "scope.symbolic2",
                                               "scope.dynamicsaved3", "scope.dynamic2"));
  }

  template <class StatType> void scopeOnce(const MakeStatFn make_stat) {
    CachedReference<StatType> symbolic2_ref(*scope_, "scope.symbolic2");
    CachedReference<StatType> dynamic2_ref(*scope_, "scope.dynamic2");
    EXPECT_FALSE(symbolic2_ref.get());
    EXPECT_FALSE(dynamic2_ref.get());

    init(make_stat);

    ASSERT_TRUE(symbolic2_ref.get());
    ASSERT_TRUE(dynamic2_ref.get());
    EXPECT_FALSE(scope_->iterate(iterOnce<StatType>()));
    EXPECT_EQ(1, results_.size());
    EXPECT_TRUE(checkValue(*symbolic2_ref.get()));
    EXPECT_TRUE(checkValue(*dynamic2_ref.get()));
  }

  template <class StatType> void scopeAll(const MakeStatFn make_stat) {
    init(make_stat);
    EXPECT_TRUE(scope_->iterate(iterAll<StatType>()));
    EXPECT_THAT(results_,
                UnorderedElementsAre("scope.symbolic2", "scope.dynamic2", "scope.dynamicsaved3"));
  }

  SymbolTablePtr symbol_table_;
  StatNamePool pool_;
  std::unique_ptr<Allocator> alloc_;
  std::unique_ptr<Store> store_;
  ScopeSharedPtr scope_;
  absl::flat_hash_set<std::string> results_;
  StatNameTagVector tags_;
};

INSTANTIATE_TEST_SUITE_P(StatsUtilityTest, StatsUtilityTest,
                         testing::ValuesIn({StoreType::ThreadLocal, StoreType::Isolated}));

TEST_P(StatsUtilityTest, Counters) {
  ScopeSharedPtr scope = store_->createScope("scope.");
  Counter& c1 = Utility::counterFromElements(*scope, {DynamicName("a"), DynamicName("b")});
  EXPECT_EQ("scope.a.b", c1.name());
  StatName token = pool_.add("token");
  Counter& c2 = Utility::counterFromElements(*scope, {DynamicName("a"), token, DynamicName("b")});
  EXPECT_EQ("scope.a.token.b", c2.name());
  StatName suffix = pool_.add("suffix");
  Counter& c3 = Utility::counterFromElements(*scope, {token, suffix});
  EXPECT_EQ("scope.token.suffix", c3.name());
  Counter& c4 = Utility::counterFromStatNames(*scope, {token, suffix});
  EXPECT_EQ("scope.token.suffix", c4.name());
  EXPECT_EQ(&c3, &c4);

  Counter& ctags =
      Utility::counterFromElements(*scope, {DynamicName("x"), token, DynamicName("y")}, tags_);
  EXPECT_EQ("scope.x.token.y.tag1.value1.tag2.value2", ctags.name());
}

// Exercises TaggedStatName directly: it pre-encodes the base name, tagged name, and tags
// into its own pool, copying the string_view inputs so callers need not keep them alive.
TEST_P(StatsUtilityTest, TaggedStatNameAccessors) {
  const std::vector<TagStringView> tag_views{{"tagA", "valA"}, {"tagB", "valB"}};
  TaggedStatName tagged(*symbol_table_, "base.name", tag_views, "base.valA.name");
  EXPECT_EQ("base.name", symbol_table_->toString(tagged.baseName()));
  EXPECT_EQ("base.valA.name", symbol_table_->toString(tagged.name()));
  const StatNameTagSpan name_tags = tagged.tags();
  ASSERT_EQ(2, name_tags.size());
  EXPECT_EQ("tagA", symbol_table_->toString(name_tags[0].first));
  EXPECT_EQ("valA", symbol_table_->toString(name_tags[0].second));
  EXPECT_EQ("tagB", symbol_table_->toString(name_tags[1].first));
  EXPECT_EQ("valB", symbol_table_->toString(name_tags[1].second));

  // Empty tags: base and tagged forms coincide and there are no tags.
  TaggedStatName empty(*symbol_table_, "base", {}, "base");
  EXPECT_EQ("base", symbol_table_->toString(empty.baseName()));
  EXPECT_EQ("base", symbol_table_->toString(empty.name()));
  EXPECT_TRUE(empty.tags().empty());

  // Empty tags with an empty tagged name: the name falls back to the base name.
  TaggedStatName fallback(*symbol_table_, "base", {}, "");
  EXPECT_EQ("base", symbol_table_->toString(fallback.baseName()));
  EXPECT_EQ("base", symbol_table_->toString(fallback.name()));
  EXPECT_TRUE(fallback.tags().empty());
}

TEST_P(StatsUtilityTest, Gauges) {
  ScopeSharedPtr scope = store_->createScope("scope.");
  Gauge& g1 = Utility::gaugeFromElements(*scope, {DynamicName("a"), DynamicName("b")},
                                         Gauge::ImportMode::NeverImport);
  EXPECT_EQ("scope.a.b", g1.name());
  EXPECT_EQ(Gauge::ImportMode::NeverImport, g1.importMode());
  StatName token = pool_.add("token");
  Gauge& g2 = Utility::gaugeFromElements(*scope, {DynamicName("a"), token, DynamicName("b")},
                                         Gauge::ImportMode::Accumulate);
  EXPECT_EQ("scope.a.token.b", g2.name());
  EXPECT_EQ(Gauge::ImportMode::Accumulate, g2.importMode());
  StatName suffix = pool_.add("suffix");
  Gauge& g3 = Utility::gaugeFromElements(*scope, {token, suffix}, Gauge::ImportMode::NeverImport);
  EXPECT_EQ("scope.token.suffix", g3.name());
  Gauge& g4 = Utility::gaugeFromStatNames(*scope, {token, suffix}, Gauge::ImportMode::NeverImport);
  EXPECT_EQ("scope.token.suffix", g4.name());
  EXPECT_EQ(&g3, &g4);
}

TEST_P(StatsUtilityTest, Histograms) {
  ScopeSharedPtr scope = store_->createScope("scope.");
  Histogram& h1 = Utility::histogramFromElements(*scope, {DynamicName("a"), DynamicName("b")},
                                                 Histogram::Unit::Milliseconds);
  EXPECT_EQ("scope.a.b", h1.name());
  EXPECT_EQ(Histogram::Unit::Milliseconds, h1.unit());
  StatName token = pool_.add("token");
  Histogram& h2 = Utility::histogramFromElements(
      *scope, {DynamicName("a"), token, DynamicName("b")}, Histogram::Unit::Microseconds);
  EXPECT_EQ("scope.a.token.b", h2.name());
  EXPECT_EQ(Histogram::Unit::Microseconds, h2.unit());
  StatName suffix = pool_.add("suffix");
  Histogram& h3 = Utility::histogramFromElements(*scope, {token, suffix}, Histogram::Unit::Bytes);
  EXPECT_EQ("scope.token.suffix", h3.name());
  EXPECT_EQ(Histogram::Unit::Bytes, h3.unit());
  Histogram& h4 = Utility::histogramFromStatNames(*scope, {token, suffix}, Histogram::Unit::Bytes);
  EXPECT_EQ(&h3, &h4);
}

TEST_P(StatsUtilityTest, TextReadouts) {
  ScopeSharedPtr scope = store_->createScope("scope.");
  TextReadout& t1 = Utility::textReadoutFromElements(*scope, {DynamicName("a"), DynamicName("b")});
  EXPECT_EQ("scope.a.b", t1.name());
  StatName token = pool_.add("token");
  TextReadout& t2 =
      Utility::textReadoutFromElements(*scope, {DynamicName("a"), token, DynamicName("b")});
  EXPECT_EQ("scope.a.token.b", t2.name());
  StatName suffix = pool_.add("suffix");
  TextReadout& t3 = Utility::textReadoutFromElements(*scope, {token, suffix});
  EXPECT_EQ("scope.token.suffix", t3.name());
  TextReadout& t4 = Utility::textReadoutFromStatNames(*scope, {token, suffix});
  EXPECT_EQ(&t3, &t4);
}

TEST_P(StatsUtilityTest, StoreCounterOnce) { storeOnce<Counter>(makeCounter()); }

TEST_P(StatsUtilityTest, StoreCounterAll) { storeAll<Counter>(makeCounter()); }

TEST_P(StatsUtilityTest, ScopeCounterOnce) { scopeOnce<Counter>(makeCounter()); }

TEST_P(StatsUtilityTest, ScopeCounterAll) { scopeAll<Counter>(makeCounter()); }

TEST_P(StatsUtilityTest, StoreGaugeOnce) { storeOnce<Gauge>(makeGauge()); }

TEST_P(StatsUtilityTest, StoreGaugeAll) { storeAll<Gauge>(makeGauge()); }

TEST_P(StatsUtilityTest, ScopeGaugeOnce) { scopeOnce<Gauge>(makeGauge()); }

TEST_P(StatsUtilityTest, ScopeGaugeAll) { scopeAll<Gauge>(makeGauge()); }

TEST_P(StatsUtilityTest, StoreHistogramOnce) { storeOnce<Histogram>(makeHistogram()); }

TEST_P(StatsUtilityTest, StoreHistogramAll) { storeAll<Histogram>(makeHistogram()); }

TEST_P(StatsUtilityTest, ScopeHistogramOnce) { scopeOnce<Histogram>(makeHistogram()); }

TEST_P(StatsUtilityTest, ScopeHistogramAll) { scopeAll<Histogram>(makeHistogram()); }

TEST_P(StatsUtilityTest, StoreTextReadoutOnce) { storeOnce<TextReadout>(makeTextReadout()); }

TEST_P(StatsUtilityTest, StoreTextReadoutAll) { storeAll<TextReadout>(makeTextReadout()); }

TEST_P(StatsUtilityTest, ScopeTextReadoutOnce) { scopeOnce<TextReadout>(makeTextReadout()); }

TEST_P(StatsUtilityTest, ScopeTextReadoutAll) { scopeAll<TextReadout>(makeTextReadout()); }

TEST_P(StatsUtilityTest, SanitizeStatsName) {
  EXPECT_EQ("a.b.c", Utility::sanitizeStatsName("a.b.c."));
  EXPECT_EQ("a.b.c", Utility::sanitizeStatsName(".a.b.c"));
  EXPECT_EQ("a__b", Utility::sanitizeStatsName("a::b"));
  EXPECT_EQ("a._", Utility::sanitizeStatsName(absl::string_view("a.\0", 3)));
  EXPECT_EQ("a_b", Utility::sanitizeStatsName("a://b"));
  EXPECT_EQ("a_b", Utility::sanitizeStatsName("a:/b"));
}

#define LITE_SCOPE_HELPER_TEST_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT)                      \
  COUNTER(requests)                                                                                \
  GAUGE(active, Accumulate)                                                                        \
  HISTOGRAM(latency, Milliseconds)                                                                 \
  TEXT_READOUT(info)

struct LiteScopeHelperTestStats {
  LITE_SCOPE_HELPER_TEST_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                               GENERATE_HISTOGRAM_STRUCT, GENERATE_TEXT_READOUT_STRUCT)
};

// A helper without a prefix or tags creates the same stats as the scope itself.
TEST_P(StatsUtilityTest, LiteScopeHelperNoPrefix) {
  LiteScopeHelper helper(*scope_);
  EXPECT_TRUE(helper.basePrefix().empty());
  EXPECT_TRUE(helper.prefix().empty());
  EXPECT_TRUE(helper.tags().empty());
  EXPECT_EQ(scope_.get(), &helper.scope());
  EXPECT_EQ(&scope_->symbolTable(), &helper.symbolTable());
  EXPECT_EQ(&scope_->constSymbolTable(), &helper.constSymbolTable());

  Counter& c = helper.counterFromString("requests");
  EXPECT_EQ("scope.requests", c.name());
  EXPECT_EQ(&c, &scope_->counterFromString("requests"));
}

// A helper with a prefix but no tags: the flat prefix is ignored, and the stats are the ones the
// scope creates for the joined name.
TEST_P(StatsUtilityTest, LiteScopeHelperPrefixOnly) {
  LiteScopeHelper helper(*scope_, "prefix.", {}, "ignored");
  EXPECT_EQ("prefix", symbol_table_->toString(helper.basePrefix()));
  EXPECT_EQ("prefix", symbol_table_->toString(helper.prefix()));
  EXPECT_TRUE(helper.tags().empty());

  Counter& c = helper.counterFromString("requests");
  EXPECT_EQ("scope.prefix.requests", c.name());
  EXPECT_EQ(&c, &scope_->counterFromString("prefix.requests"));

  Gauge& g = helper.gaugeFromString("active", Gauge::ImportMode::Accumulate);
  EXPECT_EQ("scope.prefix.active", g.name());
  EXPECT_EQ(&g, &scope_->gaugeFromString("prefix.active", Gauge::ImportMode::Accumulate));

  Histogram& h = helper.histogramFromString("latency", Histogram::Unit::Milliseconds);
  EXPECT_EQ("scope.prefix.latency", h.name());
  EXPECT_EQ(&h, &scope_->histogramFromString("prefix.latency", Histogram::Unit::Milliseconds));

  TextReadout& t = helper.textReadoutFromString("info");
  EXPECT_EQ("scope.prefix.info", t.name());
  EXPECT_EQ(&t, &scope_->textReadoutFromString("prefix.info"));
}

// A helper with string tags can be used with the POOL_* macros in place of a scope. The stats are
// created in the scope, with the tag value at its position in the flat name.
TEST_P(StatsUtilityTest, LiteScopeHelperWithMacros) {
  LiteScopeHelper helper(*scope_, "prefix", {{"tag", "value"}}, "prefix.value");
  EXPECT_EQ("prefix", symbol_table_->toString(helper.basePrefix()));
  EXPECT_EQ("prefix.value", symbol_table_->toString(helper.prefix()));
  ASSERT_EQ(1, helper.tags().size());
  EXPECT_EQ("tag", symbol_table_->toString(helper.tags()[0].first));
  EXPECT_EQ("value", symbol_table_->toString(helper.tags()[0].second));

  LiteScopeHelperTestStats stats{LITE_SCOPE_HELPER_TEST_STATS(
      POOL_COUNTER(helper), POOL_GAUGE(helper), POOL_HISTOGRAM(helper), POOL_TEXT_READOUT(helper))};
  EXPECT_EQ("scope.prefix.value.requests", stats.requests_.name());
  EXPECT_EQ("scope.prefix.value.active", stats.active_.name());
  EXPECT_EQ(Gauge::ImportMode::Accumulate, stats.active_.importMode());
  EXPECT_EQ("scope.prefix.value.latency", stats.latency_.name());
  EXPECT_EQ(Histogram::Unit::Milliseconds, stats.latency_.unit());
  EXPECT_EQ("scope.prefix.value.info", stats.info_.name());

  // The helper owns nothing: the stats are found in the scope.
  bool found = false;
  scope_->iterate(IterateFn<Counter>([&](const RefcountPtr<Counter>& counter) {
    found = found || counter.get() == &stats.requests_;
    return true;
  }));
  EXPECT_TRUE(found);

  // The isolated store always honors explicit tags.
  if (GetParam() == StoreType::Isolated) {
    EXPECT_EQ("scope.prefix.requests", stats.requests_.tagExtractedName());
    EXPECT_THAT(stats.requests_.tags(), testing::ElementsAre(Tag{"tag", "value"}));
  }

  // The macros with an additional string prefix work too.
  LiteScopeHelperTestStats prefixed_stats{LITE_SCOPE_HELPER_TEST_STATS(
      POOL_COUNTER_PREFIX(helper, "extra."), POOL_GAUGE_PREFIX(helper, "extra."),
      POOL_HISTOGRAM_PREFIX(helper, "extra."), POOL_TEXT_READOUT_PREFIX(helper, "extra."))};
  EXPECT_EQ("scope.prefix.value.extra.requests", prefixed_stats.requests_.name());
}

// A helper created from pre-encoded StatNames and used with StatName leaves.
TEST_P(StatsUtilityTest, LiteScopeHelperWithStatNames) {
  LiteScopeHelper helper(*scope_, pool_.add("prefix"), tags_, pool_.add("prefix.value1.value2"));

  Counter& c = helper.counterFromStatName(pool_.add("requests"));
  EXPECT_EQ("scope.prefix.value1.value2.requests", c.name());
  EXPECT_EQ(&c, &helper.counterFromString("requests"));

  Gauge& g = helper.gaugeFromStatName(pool_.add("active"), Gauge::ImportMode::NeverImport);
  EXPECT_EQ("scope.prefix.value1.value2.active", g.name());
  EXPECT_EQ(Gauge::ImportMode::NeverImport, g.importMode());

  Histogram& h = helper.histogramFromStatName(pool_.add("latency"), Histogram::Unit::Bytes);
  EXPECT_EQ("scope.prefix.value1.value2.latency", h.name());
  EXPECT_EQ(Histogram::Unit::Bytes, h.unit());

  TextReadout& t = helper.textReadoutFromStatName(pool_.add("info"));
  EXPECT_EQ("scope.prefix.value1.value2.info", t.name());

  if (GetParam() == StoreType::Isolated) {
    EXPECT_EQ("scope.prefix.requests", c.tagExtractedName());
    EXPECT_THAT(c.tags(), testing::ElementsAre(Tag{"tag1", "value1"}, Tag{"tag2", "value2"}));
  }
}

// A helper created from a pre-encoded StatName prefix without tags: the flat prefix argument is
// ignored and the base prefix drives both forms. The names are referenced rather than copied, so
// the pool owning them (here the fixture's pool_) must outlive the helper.
TEST_P(StatsUtilityTest, LiteScopeHelperStatNamePrefixOnly) {
  const StatName base = pool_.add("prefix");
  LiteScopeHelper helper(*scope_, base, {}, pool_.add("ignored"));
  EXPECT_EQ(base, helper.basePrefix());
  EXPECT_EQ(base, helper.prefix());
  EXPECT_TRUE(helper.tags().empty());

  Counter& c = helper.counterFromStatName(pool_.add("requests"));
  EXPECT_EQ("scope.prefix.requests", c.name());
  EXPECT_EQ(&c, &scope_->counterFromString("prefix.requests"));
}

} // namespace
} // namespace Stats
} // namespace Envoy
