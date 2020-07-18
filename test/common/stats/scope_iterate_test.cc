#include <memory>
#include <string>

#include "common/stats/isolated_store_impl.h"
#include "common/stats/symbol_table_creator.h"
#include "common/stats/thread_local_store.h"

//#include "test/common/stats/stat_test_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::UnorderedElementsAre;

namespace Envoy {
namespace Stats {

// All the scope iterator tests should be run for both IsolatedStore and ThreadLocalStore.
enum class StoreType {
  ThreadLocal,
  Isolated,
};

class ScopeIterateTest : public testing::TestWithParam<StoreType> {
public:
  template <class StatType>
  using IterateFn = std::function<bool(const RefcountPtr<StatType>& stat)>;
  using MakeStatFn = std::function<void(Scope& scope, const ElementVec& elements)>;

  ScopeIterateTest() : symbol_table_(SymbolTableCreator::makeSymbolTable()), pool_(*symbol_table_) {
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
      Utility::counterFromElements(scope, elements);
    };
  }

  static MakeStatFn makeGauge() {
    return [](Scope& scope, const ElementVec& elements) {
      Utility::gaugeFromElements(scope, elements, Gauge::ImportMode::Accumulate);
    };
  }

  static MakeStatFn makeHistogram() {
    return [](Scope& scope, const ElementVec& elements) {
      Utility::histogramFromElements(scope, elements, Histogram::Unit::Unspecified);
    };
  }

  static MakeStatFn makeTextReadout() {
    return [](Scope& scope, const ElementVec& elements) {
      Utility::textReadoutFromElements(scope, elements);
    };
  }

  template <class StatType> void storeOnce(const MakeStatFn make_stat) {
    CachedReference<StatType> symbolic1_ref(*store_, "symbolic1");
    CachedReference<StatType> dynamic1_ref(*store_, "dynamic1");
    EXPECT_FALSE(symbolic1_ref.find());
    EXPECT_FALSE(dynamic1_ref.find());

    init(make_stat);

    ASSERT_TRUE(symbolic1_ref.find());
    ASSERT_TRUE(dynamic1_ref.find());
    EXPECT_FALSE(store_->iterate(iterOnce<StatType>()));
    EXPECT_EQ(1, results_.size());
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
    EXPECT_FALSE(symbolic2_ref.find());
    EXPECT_FALSE(dynamic2_ref.find());

    init(make_stat);

    ASSERT_TRUE(symbolic2_ref.find());
    ASSERT_TRUE(dynamic2_ref.find());
    EXPECT_FALSE(scope_->iterate(iterOnce<StatType>()));
    EXPECT_EQ(1, results_.size());
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
};

INSTANTIATE_TEST_SUITE_P(ScopeIterateTest, ScopeIterateTest,
                         testing::ValuesIn({StoreType::ThreadLocal, StoreType::Isolated}));

TEST_P(ScopeIterateTest, StoreCounterOnce) { storeOnce<Counter>(makeCounter()); }

TEST_P(ScopeIterateTest, StoreCounterAll) { storeAll<Counter>(makeCounter()); }

TEST_P(ScopeIterateTest, ScopeCounterOnce) { scopeOnce<Counter>(makeCounter()); }

TEST_P(ScopeIterateTest, ScopeCounterAll) { scopeAll<Counter>(makeCounter()); }

TEST_P(ScopeIterateTest, StoreGaugeOnce) { storeOnce<Gauge>(makeGauge()); }

TEST_P(ScopeIterateTest, StoreGaugeAll) { storeAll<Gauge>(makeGauge()); }

TEST_P(ScopeIterateTest, ScopeGaugeOnce) { scopeOnce<Gauge>(makeGauge()); }

TEST_P(ScopeIterateTest, ScopeGaugeAll) { scopeAll<Gauge>(makeGauge()); }

TEST_P(ScopeIterateTest, StoreHistogramOnce) { storeOnce<Histogram>(makeHistogram()); }

TEST_P(ScopeIterateTest, StoreHistogramAll) { storeAll<Histogram>(makeHistogram()); }

TEST_P(ScopeIterateTest, ScopeHistogramOnce) { scopeOnce<Histogram>(makeHistogram()); }

TEST_P(ScopeIterateTest, ScopeHistogramAll) { scopeAll<Histogram>(makeHistogram()); }

TEST_P(ScopeIterateTest, StoreTextReadoutOnce) { storeOnce<TextReadout>(makeTextReadout()); }

TEST_P(ScopeIterateTest, StoreTextReadoutAll) { storeAll<TextReadout>(makeTextReadout()); }

TEST_P(ScopeIterateTest, ScopeTextReadoutOnce) { scopeOnce<TextReadout>(makeTextReadout()); }

TEST_P(ScopeIterateTest, ScopeTextReadoutAll) { scopeAll<TextReadout>(makeTextReadout()); }

} // namespace Stats
} // namespace Envoy
