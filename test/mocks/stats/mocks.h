#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/stats/histogram.h"
#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_matcher.h"
#include "envoy/stats/store.h"
#include "envoy/stats/timespan.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/stats/fake_symbol_table_impl.h"
#include "common/stats/histogram_impl.h"
#include "common/stats/isolated_store_impl.h"
#include "common/stats/store_impl.h"
#include "common/stats/symbol_table_creator.h"
#include "common/stats/timespan_impl.h"

#include "test/test_common/global.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Stats {

class TestSymbolTableHelper {
public:
  TestSymbolTableHelper() : symbol_table_(SymbolTableCreator::makeSymbolTable()) {}
  SymbolTable& symbolTable() { return *symbol_table_; }
  const SymbolTable& constSymbolTable() const { return *symbol_table_; }

private:
  SymbolTablePtr symbol_table_;
};

class TestSymbolTable {
public:
  SymbolTable& operator*() { return global_.get().symbolTable(); }
  const SymbolTable& operator*() const { return global_.get().constSymbolTable(); }
  SymbolTable* operator->() { return &global_.get().symbolTable(); }
  const SymbolTable* operator->() const { return &global_.get().constSymbolTable(); }
  Envoy::Test::Global<TestSymbolTableHelper> global_;
};

template <class BaseClass> class MockMetric : public BaseClass {
public:
  MockMetric() : name_(*this), tag_pool_(*symbol_table_) {}
  ~MockMetric() override = default;

  // This bit of C++ subterfuge allows us to support the wealth of tests that
  // do metric->name_ = "foo" even though names are more complex now. Note
  // that the statName is only populated if there is a symbol table.
  class MetricName {
  public:
    explicit MetricName(MockMetric& mock_metric) : mock_metric_(mock_metric) {}
    ~MetricName() {
      if (stat_name_storage_ != nullptr) {
        stat_name_storage_->free(mock_metric_.symbolTable());
      }
    }

    void operator=(absl::string_view str) {
      name_ = std::string(str);
      stat_name_storage_ = std::make_unique<StatNameStorage>(str, mock_metric_.symbolTable());
    }

    std::string name() const { return name_; }
    StatName statName() const { return stat_name_storage_->statName(); }

  private:
    MockMetric& mock_metric_;
    std::string name_;
    std::unique_ptr<StatNameStorage> stat_name_storage_;
  };

  SymbolTable& symbolTable() override { return *symbol_table_; }
  const SymbolTable& constSymbolTable() const override { return *symbol_table_; }

  // Note: cannot be mocked because it is accessed as a Property in a gmock EXPECT_CALL. This
  // creates a deadlock in gmock and is an unintended use of mock functions.
  std::string name() const override { return name_.name(); }
  StatName statName() const override { return name_.statName(); }
  std::vector<Tag> tags() const override { return tags_; }
  void setTagExtractedName(absl::string_view name) {
    tag_extracted_name_ = std::string(name);
    tag_extracted_stat_name_ =
        std::make_unique<StatNameManagedStorage>(tagExtractedName(), *symbol_table_);
  }
  std::string tagExtractedName() const override {
    return tag_extracted_name_.empty() ? name() : tag_extracted_name_;
  }
  StatName tagExtractedStatName() const override { return tag_extracted_stat_name_->statName(); }
  void iterateTagStatNames(const Metric::TagStatNameIterFn& fn) const override {
    ASSERT((tag_names_and_values_.size() % 2) == 0);
    for (size_t i = 0; i < tag_names_and_values_.size(); i += 2) {
      if (!fn(tag_names_and_values_[i], tag_names_and_values_[i + 1])) {
        return;
      }
    }
  }

  TestSymbolTable symbol_table_; // Must outlive name_.
  MetricName name_;

  void setTags(const std::vector<Tag>& tags) {
    tag_pool_.clear();
    tags_ = tags;
    for (const Tag& tag : tags) {
      tag_names_and_values_.push_back(tag_pool_.add(tag.name_));
      tag_names_and_values_.push_back(tag_pool_.add(tag.value_));
    }
  }
  void addTag(const Tag& tag) {
    tags_.emplace_back(tag);
    tag_names_and_values_.push_back(tag_pool_.add(tag.name_));
    tag_names_and_values_.push_back(tag_pool_.add(tag.value_));
  }

private:
  std::vector<Tag> tags_;
  std::vector<StatName> tag_names_and_values_;
  std::string tag_extracted_name_;
  StatNamePool tag_pool_;
  std::unique_ptr<StatNameManagedStorage> tag_extracted_stat_name_;
};

template <class BaseClass> class MockStatWithRefcount : public MockMetric<BaseClass> {
public:
  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

  RefcountHelper refcount_helper_;
};

class MockCounter : public MockStatWithRefcount<Counter> {
public:
  MockCounter();
  ~MockCounter() override;

  MOCK_METHOD1(add, void(uint64_t amount));
  MOCK_METHOD0(inc, void());
  MOCK_METHOD0(latch, uint64_t());
  MOCK_METHOD0(reset, void());
  MOCK_CONST_METHOD0(used, bool());
  MOCK_CONST_METHOD0(value, uint64_t());

  bool used_;
  uint64_t value_;
  uint64_t latch_;

  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

private:
  RefcountHelper refcount_helper_;
};

class MockGauge : public MockStatWithRefcount<Gauge> {
public:
  MockGauge();
  ~MockGauge() override;

  MOCK_METHOD1(add, void(uint64_t amount));
  MOCK_METHOD0(dec, void());
  MOCK_METHOD0(inc, void());
  MOCK_METHOD1(set, void(uint64_t value));
  MOCK_METHOD1(sub, void(uint64_t amount));
  MOCK_METHOD1(mergeImportMode, void(ImportMode));
  MOCK_CONST_METHOD0(used, bool());
  MOCK_CONST_METHOD0(value, uint64_t());
  MOCK_CONST_METHOD0(cachedShouldImport, absl::optional<bool>());
  MOCK_CONST_METHOD0(importMode, ImportMode());

  bool used_;
  uint64_t value_;
  ImportMode import_mode_;

  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

private:
  RefcountHelper refcount_helper_;
};

class MockHistogram : public MockMetric<Histogram> {
public:
  MockHistogram();
  ~MockHistogram() override;

  MOCK_CONST_METHOD0(used, bool());
  MOCK_CONST_METHOD0(unit, Histogram::Unit());
  MOCK_METHOD1(recordValue, void(uint64_t value));

  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

  Unit unit_{Histogram::Unit::Unspecified};
  Store* store_;

private:
  RefcountHelper refcount_helper_;
};

class MockParentHistogram : public MockMetric<ParentHistogram> {
public:
  MockParentHistogram();
  ~MockParentHistogram() override;

  void merge() override {}
  const std::string quantileSummary() const override { return ""; };
  const std::string bucketSummary() const override { return ""; };

  MOCK_CONST_METHOD0(used, bool());
  MOCK_CONST_METHOD0(unit, Histogram::Unit());
  MOCK_METHOD1(recordValue, void(uint64_t value));
  MOCK_CONST_METHOD0(cumulativeStatistics, const HistogramStatistics&());
  MOCK_CONST_METHOD0(intervalStatistics, const HistogramStatistics&());

  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

  bool used_;
  Unit unit_{Histogram::Unit::Unspecified};
  Store* store_;
  std::shared_ptr<HistogramStatistics> histogram_stats_ =
      std::make_shared<HistogramStatisticsImpl>();

private:
  RefcountHelper refcount_helper_;
};

class MockMetricSnapshot : public MetricSnapshot {
public:
  MockMetricSnapshot();
  ~MockMetricSnapshot() override;

  MOCK_METHOD0(counters, const std::vector<CounterSnapshot>&());
  MOCK_METHOD0(gauges, const std::vector<std::reference_wrapper<const Gauge>>&());
  MOCK_METHOD0(histograms, const std::vector<std::reference_wrapper<const ParentHistogram>>&());

  std::vector<CounterSnapshot> counters_;
  std::vector<std::reference_wrapper<const Gauge>> gauges_;
  std::vector<std::reference_wrapper<const ParentHistogram>> histograms_;
};

class MockSink : public Sink {
public:
  MockSink();
  ~MockSink() override;

  MOCK_METHOD1(flush, void(MetricSnapshot& snapshot));
  MOCK_METHOD2(onHistogramComplete, void(const Histogram& histogram, uint64_t value));
};

class SymbolTableProvider {
public:
  TestSymbolTable global_symbol_table_;
};

class MockStore : public SymbolTableProvider, public StoreImpl {
public:
  MockStore();
  ~MockStore() override;

  ScopePtr createScope(const std::string& name) override { return ScopePtr{createScope_(name)}; }

  MOCK_METHOD2(deliverHistogramToSinks, void(const Histogram& histogram, uint64_t value));
  MOCK_METHOD1(counter, Counter&(const std::string&));
  MOCK_CONST_METHOD0(counters, std::vector<CounterSharedPtr>());
  MOCK_METHOD1(createScope_, Scope*(const std::string& name));
  MOCK_METHOD2(gauge, Gauge&(const std::string&, Gauge::ImportMode));
  MOCK_METHOD1(nullGauge, NullGaugeImpl&(const std::string&));
  MOCK_CONST_METHOD0(gauges, std::vector<GaugeSharedPtr>());
  MOCK_METHOD2(histogram, Histogram&(const std::string&, Histogram::Unit));
  MOCK_CONST_METHOD0(histograms, std::vector<ParentHistogramSharedPtr>());

  MOCK_CONST_METHOD1(findCounter, OptionalCounter(StatName));
  MOCK_CONST_METHOD1(findGauge, OptionalGauge(StatName));
  MOCK_CONST_METHOD1(findHistogram, OptionalHistogram(StatName));

  Counter& counterFromStatName(StatName name) override {
    return counter(symbol_table_->toString(name));
  }
  Gauge& gaugeFromStatName(StatName name, Gauge::ImportMode import_mode) override {
    return gauge(symbol_table_->toString(name), import_mode);
  }
  Histogram& histogramFromStatName(StatName name, Histogram::Unit unit) override {
    return histogram(symbol_table_->toString(name), unit);
  }

  TestSymbolTable symbol_table_;
  testing::NiceMock<MockCounter> counter_;
  std::vector<std::unique_ptr<MockHistogram>> histograms_;
};

/**
 * With IsolatedStoreImpl it's hard to test timing stats.
 * MockIsolatedStatsStore mocks only deliverHistogramToSinks for better testing.
 */
class MockIsolatedStatsStore : public SymbolTableProvider, public IsolatedStoreImpl {
public:
  MockIsolatedStatsStore();
  ~MockIsolatedStatsStore() override;

  MOCK_METHOD2(deliverHistogramToSinks, void(const Histogram& histogram, uint64_t value));
};

class MockStatsMatcher : public StatsMatcher {
public:
  MockStatsMatcher();
  ~MockStatsMatcher() override;
  MOCK_CONST_METHOD1(rejects, bool(const std::string& name));
  bool acceptsAll() const override { return accepts_all_; }
  bool rejectsAll() const override { return rejects_all_; }

  bool accepts_all_{false};
  bool rejects_all_{false};
};

} // namespace Stats
} // namespace Envoy
