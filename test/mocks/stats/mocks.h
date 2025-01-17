#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
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

#include "source/common/stats/histogram_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/timespan_impl.h"

#include "test/common/stats/stat_test_utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Stats {

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
  TagVector tags() const override { return tags_; }
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

  TestUtil::TestSymbolTable symbol_table_; // Must outlive name_.
  MetricName name_;

  void setTags(const TagVector& tags) {
    tag_pool_.clear();
    tag_names_and_values_.clear();
    tags_ = tags;
    for (const Tag& tag : tags) {
      tag_names_and_values_.push_back(tag_pool_.add(tag.name_));
      tag_names_and_values_.push_back(tag_pool_.add(tag.value_));
    }
  }

  void setTags(const Stats::StatNameTagVector& tags) {
    tag_pool_.clear();
    tag_names_and_values_.clear();
    tags_.clear();
    for (const StatNameTag& tag : tags) {
      tag_names_and_values_.push_back(tag.first);
      tag_names_and_values_.push_back(tag.second);
      tags_.push_back(Tag{symbol_table_->toString(tag.first), symbol_table_->toString(tag.second)});
    }
  }

  void addTag(const Tag& tag) {
    tags_.emplace_back(tag);
    tag_names_and_values_.push_back(tag_pool_.add(tag.name_));
    tag_names_and_values_.push_back(tag_pool_.add(tag.value_));
  }

private:
  TagVector tags_;
  StatNameVec tag_names_and_values_;
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

  MOCK_METHOD(void, add, (uint64_t amount));
  MOCK_METHOD(void, inc, ());
  MOCK_METHOD(uint64_t, latch, ());
  MOCK_METHOD(void, reset, ());
  MOCK_METHOD(bool, used, (), (const));
  MOCK_METHOD(bool, hidden, (), (const));
  MOCK_METHOD(uint64_t, value, (), (const));

  bool used_;
  bool hidden_;
  uint64_t value_;
  uint64_t latch_;
};

class MockGauge : public MockStatWithRefcount<Gauge> {
public:
  MockGauge();
  ~MockGauge() override;

  MOCK_METHOD(void, add, (uint64_t amount));
  MOCK_METHOD(void, dec, ());
  MOCK_METHOD(void, inc, ());
  MOCK_METHOD(void, set, (uint64_t value));
  MOCK_METHOD(void, setParentValue, (uint64_t parent_value));
  MOCK_METHOD(void, sub, (uint64_t amount));
  MOCK_METHOD(void, mergeImportMode, (ImportMode));
  MOCK_METHOD(bool, used, (), (const));
  MOCK_METHOD(bool, hidden, (), (const));
  MOCK_METHOD(uint64_t, value, (), (const));
  MOCK_METHOD(absl::optional<bool>, cachedShouldImport, (), (const));
  MOCK_METHOD(ImportMode, importMode, (), (const));

  bool used_;
  bool hidden_;
  uint64_t value_;
  ImportMode import_mode_;
};

class MockHistogram : public MockMetric<Histogram> {
public:
  MockHistogram();
  ~MockHistogram() override;

  MOCK_METHOD(bool, used, (), (const));
  MOCK_METHOD(bool, hidden, (), (const));
  MOCK_METHOD(Histogram::Unit, unit, (), (const));
  MOCK_METHOD(void, recordValue, (uint64_t value));

  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

  Unit unit_{Histogram::Unit::Unspecified};
  Store* store_{};

private:
  RefcountHelper refcount_helper_;
};

class MockParentHistogram : public MockMetric<ParentHistogram> {
public:
  MockParentHistogram();
  ~MockParentHistogram() override;

  void merge() override {}
  std::string quantileSummary() const override { return ""; };
  std::string bucketSummary() const override { return ""; };
  MOCK_METHOD(bool, used, (), (const));
  MOCK_METHOD(bool, hidden, (), (const));
  MOCK_METHOD(Histogram::Unit, unit, (), (const));
  MOCK_METHOD(void, recordValue, (uint64_t value));
  MOCK_METHOD(const HistogramStatistics&, cumulativeStatistics, (), (const));
  MOCK_METHOD(const HistogramStatistics&, intervalStatistics, (), (const));
  MOCK_METHOD(std::vector<Bucket>, detailedTotalBuckets, (), (const));
  MOCK_METHOD(std::vector<Bucket>, detailedIntervalBuckets, (), (const));

  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

  bool used_;
  bool hidden_;
  Unit unit_{Histogram::Unit::Unspecified};
  Store* store_{};
  std::shared_ptr<HistogramStatistics> histogram_stats_ =
      std::make_shared<HistogramStatisticsImpl>();

private:
  RefcountHelper refcount_helper_;
};

class MockTextReadout : public MockMetric<TextReadout> {
public:
  MockTextReadout();
  ~MockTextReadout() override;

  MOCK_METHOD(void, set, (absl::string_view value), (override));
  MOCK_METHOD(bool, used, (), (const, override));
  MOCK_METHOD(bool, hidden, (), (const));
  MOCK_METHOD(std::string, value, (), (const, override));

  bool used_;
  bool hidden_;
  std::string value_;
};

class MockMetricSnapshot : public MetricSnapshot {
public:
  MockMetricSnapshot();
  ~MockMetricSnapshot() override;

  MOCK_METHOD(const std::vector<CounterSnapshot>&, counters, ());
  MOCK_METHOD(const std::vector<std::reference_wrapper<const Gauge>>&, gauges, ());
  MOCK_METHOD(const std::vector<std::reference_wrapper<const ParentHistogram>>&, histograms, ());
  MOCK_METHOD(const std::vector<std::reference_wrapper<const TextReadout>>&, textReadouts, ());
  MOCK_METHOD(const std::vector<Stats::PrimitiveCounterSnapshot>&, hostCounters, ());
  MOCK_METHOD(const std::vector<Stats::PrimitiveGaugeSnapshot>&, hostGauges, ());
  MOCK_METHOD(SystemTime, snapshotTime, (), (const));

  std::vector<CounterSnapshot> counters_;
  std::vector<std::reference_wrapper<const Gauge>> gauges_;
  std::vector<std::reference_wrapper<const ParentHistogram>> histograms_;
  std::vector<std::reference_wrapper<const TextReadout>> text_readouts_;
  std::vector<Stats::PrimitiveCounterSnapshot> host_counters_;
  std::vector<Stats::PrimitiveGaugeSnapshot> host_gauges_;
  SystemTime snapshot_time_;
};

class MockSink : public Sink {
public:
  MockSink();
  ~MockSink() override;

  MOCK_METHOD(void, flush, (MetricSnapshot & snapshot));
  MOCK_METHOD(void, onHistogramComplete, (const Histogram& histogram, uint64_t value));
};

class MockSinkPredicates : public SinkPredicates {
public:
  MockSinkPredicates();
  ~MockSinkPredicates() override;
  MOCK_METHOD(bool, includeCounter, (const Counter&));
  MOCK_METHOD(bool, includeGauge, (const Gauge&));
  MOCK_METHOD(bool, includeTextReadout, (const TextReadout&));
  MOCK_METHOD(bool, includeHistogram, (const Histogram&));
};

class MockStore;

class MockScope : public TestUtil::TestScope {
public:
  MockScope(StatName prefix, MockStore& store);

  ScopeSharedPtr createScope(const std::string& name) override {
    return ScopeSharedPtr(createScope_(name));
  }
  ScopeSharedPtr scopeFromStatName(StatName name) override {
    return createScope_(symbolTable().toString(name));
  }

  MOCK_METHOD(ScopeSharedPtr, createScope_, (const std::string& name));
  MOCK_METHOD(CounterOptConstRef, findCounter, (StatName), (const));
  MOCK_METHOD(GaugeOptConstRef, findGauge, (StatName), (const));
  MOCK_METHOD(HistogramOptConstRef, findHistogram, (StatName), (const));
  MOCK_METHOD(TextReadoutOptConstRef, findTextReadout, (StatName), (const));

  // Override the lowest level of stat creation based on StatName to redirect
  // back to the old string-based mechanisms still on the MockStore object
  // to allow tests to inject EXPECT_CALL hooks for those.
  Counter& counterFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef) override;
  Gauge& gaugeFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef,
                                   Gauge::ImportMode import_mode) override;
  Histogram& histogramFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef,
                                           Histogram::Unit unit) override;
  TextReadout& textReadoutFromStatNameWithTags(const StatName& name,
                                               StatNameTagVectorOptConstRef) override;

  MockStore& mock_store_;
};

class MockStore : public TestUtil::TestStore {
public:
  MockStore();
  ~MockStore() override;

  // Store
  MOCK_METHOD(void, forEachCounter, (SizeFn, StatFn<Counter>), (const));
  MOCK_METHOD(void, forEachGauge, (SizeFn, StatFn<Gauge>), (const));
  MOCK_METHOD(void, forEachTextReadout, (SizeFn, StatFn<TextReadout>), (const));
  MOCK_METHOD(void, forEachHistogram, (SizeFn, StatFn<ParentHistogram>), (const));
  MOCK_METHOD(void, forEachSinkedHistogram, (SizeFn, StatFn<ParentHistogram>), (const));
  MOCK_METHOD(Counter&, counter, (const std::string&));
  MOCK_METHOD(Gauge&, gauge, (const std::string&, Gauge::ImportMode));
  MOCK_METHOD(Histogram&, histogram, (const std::string&, Histogram::Unit));
  MOCK_METHOD(void, deliverHistogramToSinks, (const Histogram& histogram, uint64_t value));
  MOCK_METHOD(TextReadout&, textReadout, (const std::string&));

  MockScope& mockScope() {
    MockScope* scope = dynamic_cast<MockScope*>(rootScope().get());
    ASSERT(scope != nullptr);
    return *scope;
  }

  ScopeSharedPtr makeScope(StatName name) override;

  TestUtil::TestSymbolTable symbol_table_;
  testing::NiceMock<MockCounter> counter_;
  testing::NiceMock<MockGauge> gauge_;
  std::vector<std::unique_ptr<MockHistogram>> histograms_;
};

/**
 * With IsolatedStoreImpl it's hard to test timing stats.
 * MockIsolatedStatsStore mocks only deliverHistogramToSinks for better testing.
 */
class MockIsolatedStatsStore : public TestUtil::TestStore {
public:
  MockIsolatedStatsStore();
  ~MockIsolatedStatsStore() override;

  MOCK_METHOD(void, deliverHistogramToSinks, (const Histogram& histogram, uint64_t value));
  MOCK_METHOD(const TagVector&, fixedTags, ());
};

class MockStatsMatcher : public StatsMatcher {
public:
  MockStatsMatcher();
  ~MockStatsMatcher() override;
  MOCK_METHOD(bool, rejects, (StatName name), (const));
  MOCK_METHOD(StatsMatcher::FastResult, fastRejects, (StatName name), (const));
  MOCK_METHOD(bool, slowRejects, (FastResult, StatName name), (const));
  bool acceptsAll() const override { return accepts_all_; }
  bool rejectsAll() const override { return rejects_all_; }

  bool accepts_all_{false};
  bool rejects_all_{false};
};

} // namespace Stats
} // namespace Envoy
