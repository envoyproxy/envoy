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
  using MakeStatFn = std::function<void(Scope& scope, absl::string_view name)>;

  ScopeIterateTest() : symbol_table_(SymbolTableCreator::makeSymbolTable()) {
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
    make_stat(*store_, "stat1");
    make_stat(*store_, "stat2");
    make_stat(*scope_, "stat3");
    make_stat(*scope_, "stat4");
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
    return [](Scope& scope, absl::string_view name) { scope.counterFromString(std::string(name)); };
  }

  static MakeStatFn makeGauge() {
    return [](Scope& scope, absl::string_view name) {
      scope.gaugeFromString(std::string(name), Gauge::ImportMode::Accumulate);
    };
  }

  template <class StatType> void storeOnce(const MakeStatFn make_stat) {
    init(make_stat);
    EXPECT_FALSE(store_->iterate(iterOnce<StatType>()));
    EXPECT_EQ(1, results_.size());
  }

  template <class StatType> void storeAll(const MakeStatFn make_stat) {
    init(make_stat);
    EXPECT_TRUE(store_->iterate(iterAll<StatType>()));
    EXPECT_THAT(results_, UnorderedElementsAre("stat1", "stat2", "scope.stat3", "scope.stat4"));
  }

  template <class StatType> void scopeOnce(const MakeStatFn make_stat) {
    init(make_stat);
    EXPECT_FALSE(scope_->iterate(iterOnce<StatType>()));
    EXPECT_EQ(1, results_.size());
  }

  template <class StatType> void scopeAll(const MakeStatFn make_stat) {
    init(make_stat);
    EXPECT_TRUE(scope_->iterate(iterAll<StatType>()));
    EXPECT_THAT(results_, UnorderedElementsAre("scope.stat3", "scope.stat4"));
  }

  SymbolTablePtr symbol_table_;
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

} // namespace Stats
} // namespace Envoy
