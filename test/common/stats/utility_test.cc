#include <string>

#include "envoy/stats/stats_macros.h"

#include "common/stats/isolated_store_impl.h"
#include "common/stats/null_counter.h"
#include "common/stats/null_gauge.h"
#include "common/stats/thread_local_store.h"

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
      alloc_ = std::make_unique<AllocatorImpl>(*symbol_table_),
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
    make_stat(*store_, {pool_.add("symbolic1")});
    make_stat(*store_, {Stats::DynamicName("dynamic1")});
    make_stat(*scope_, {pool_.add("symbolic2")});
    make_stat(*scope_, {Stats::DynamicName("dynamic2")});
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
    CachedReference<StatType> symbolic1_ref(*store_, "symbolic1");
    CachedReference<StatType> dynamic1_ref(*store_, "dynamic1");
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
    EXPECT_THAT(results_,
                UnorderedElementsAre("symbolic1", "dynamic1", "scope.symbolic2", "scope.dynamic2"));
  }

  template <class StatType> void scopeOnce(const MakeStatFn make_stat) {
    CachedReference<StatType> symbolic2_ref(*store_, "scope.symbolic2");
    CachedReference<StatType> dynamic2_ref(*store_, "scope.dynamic2");
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
    EXPECT_THAT(results_, UnorderedElementsAre("scope.symbolic2", "scope.dynamic2"));
  }

  SymbolTablePtr symbol_table_;
  StatNamePool pool_;
  std::unique_ptr<AllocatorImpl> alloc_;
  std::unique_ptr<Store> store_;
  ScopePtr scope_;
  absl::flat_hash_set<std::string> results_;
  StatNameTagVector tags_;
};

INSTANTIATE_TEST_SUITE_P(StatsUtilityTest, StatsUtilityTest,
                         testing::ValuesIn({StoreType::ThreadLocal, StoreType::Isolated}));

TEST_P(StatsUtilityTest, Counters) {
  ScopePtr scope = store_->createScope("scope.");
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

TEST_P(StatsUtilityTest, Gauges) {
  ScopePtr scope = store_->createScope("scope.");
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
  ScopePtr scope = store_->createScope("scope.");
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
  ScopePtr scope = store_->createScope("scope.");
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

} // namespace
} // namespace Stats
} // namespace Envoy
