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

#include "test/test_common/global.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Stats {

class MockMetric : public virtual Metric {
public:
  MockMetric();
  ~MockMetric();

  // This bit of C++ subterfuge allows us to support the wealth of tests that
  // do metric->name_ = "foo" even though names are more complex now. Note
  // that the statName is only populated if there is a symbol table.
  class MetricName {
  public:
    explicit MetricName(MockMetric& mock_metric) : mock_metric_(mock_metric) {}
    ~MetricName();

    void operator=(absl::string_view str);

    std::string name() const { return name_; }
    StatName statName() const { return stat_name_storage_->statName(); }

  private:
    MockMetric& mock_metric_;
    std::string name_;
    std::unique_ptr<StatNameStorage> stat_name_storage_;
  };

  SymbolTable& symbolTable() override { return symbol_table_.get(); }
  const SymbolTable& constSymbolTable() const override { return symbol_table_.get(); }

  // Note: cannot be mocked because it is accessed as a Property in a gmock EXPECT_CALL. This
  // creates a deadlock in gmock and is an unintended use of mock functions.
  std::string name() const override { return name_.name(); }
  StatName statName() const override { return name_.statName(); }
  std::vector<Tag> tags() const override { return tags_; }
  void setTagExtractedName(absl::string_view name);
  std::string tagExtractedName() const override {
    return tag_extracted_name_.empty() ? name() : tag_extracted_name_;
  }
  StatName tagExtractedStatName() const override { return tag_extracted_stat_name_->statName(); }
  void iterateTagStatNames(const TagStatNameIterFn& fn) const override;
  void iterateTags(const TagIterFn& fn) const override;

  Test::Global<FakeSymbolTableImpl> symbol_table_; // Must outlive name_.
  MetricName name_;

  void setTags(const std::vector<Tag>& tags);
  void addTag(const Tag& tag);

private:
  std::vector<Tag> tags_;
  std::vector<StatName> tag_names_and_values_;
  std::string tag_extracted_name_;
  StatNamePool tag_pool_;
  std::unique_ptr<StatNameManagedStorage> tag_extracted_stat_name_;
};

class MockCounter : public Counter, public MockMetric {
public:
  MockCounter();
  ~MockCounter();

  MOCK_METHOD1(add, void(uint64_t amount));
  MOCK_METHOD0(inc, void());
  MOCK_METHOD0(latch, uint64_t());
  MOCK_METHOD0(reset, void());
  MOCK_CONST_METHOD0(used, bool());
  MOCK_CONST_METHOD0(value, uint64_t());

  bool used_;
  uint64_t value_;
  uint64_t latch_;
};

class MockGauge : public Gauge, public MockMetric {
public:
  MockGauge();
  ~MockGauge();

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
};

class MockHistogram : public Histogram, public MockMetric {
public:
  MockHistogram();
  ~MockHistogram();

  MOCK_METHOD1(recordValue, void(uint64_t value));
  MOCK_CONST_METHOD0(used, bool());

  Store* store_;
};

class MockParentHistogram : public ParentHistogram, public MockMetric {
public:
  MockParentHistogram();
  ~MockParentHistogram();

  void merge() override {}
  const std::string quantileSummary() const override { return ""; };
  const std::string bucketSummary() const override { return ""; };

  MOCK_CONST_METHOD0(used, bool());
  MOCK_METHOD1(recordValue, void(uint64_t value));
  MOCK_CONST_METHOD0(cumulativeStatistics, const HistogramStatistics&());
  MOCK_CONST_METHOD0(intervalStatistics, const HistogramStatistics&());

  bool used_;
  Store* store_;
  std::shared_ptr<HistogramStatistics> histogram_stats_ =
      std::make_shared<HistogramStatisticsImpl>();
};

class MockMetricSnapshot : public MetricSnapshot {
public:
  MockMetricSnapshot();
  ~MockMetricSnapshot();

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
  ~MockSink();

  MOCK_METHOD1(flush, void(MetricSnapshot& snapshot));
  MOCK_METHOD2(onHistogramComplete, void(const Histogram& histogram, uint64_t value));
};

class SymbolTableProvider {
public:
  Test::Global<FakeSymbolTableImpl> fake_symbol_table_;
};

class MockStore : public SymbolTableProvider, public StoreImpl {
public:
  MockStore();
  ~MockStore();

  ScopePtr createScope(const std::string& name) override { return ScopePtr{createScope_(name)}; }

  MOCK_METHOD2(deliverHistogramToSinks, void(const Histogram& histogram, uint64_t value));
  MOCK_METHOD1(counter, Counter&(const std::string&));
  MOCK_CONST_METHOD0(counters, std::vector<CounterSharedPtr>());
  MOCK_METHOD1(createScope_, Scope*(const std::string& name));
  MOCK_METHOD2(gauge, Gauge&(const std::string&, Gauge::ImportMode));
  MOCK_METHOD1(nullGauge, NullGaugeImpl&(const std::string&));
  MOCK_CONST_METHOD0(gauges, std::vector<GaugeSharedPtr>());
  MOCK_METHOD1(histogram, Histogram&(const std::string& name));
  MOCK_CONST_METHOD0(histograms, std::vector<ParentHistogramSharedPtr>());

  MOCK_CONST_METHOD1(findCounter, absl::optional<std::reference_wrapper<const Counter>>(StatName));
  MOCK_CONST_METHOD1(findGauge, absl::optional<std::reference_wrapper<const Gauge>>(StatName));
  MOCK_CONST_METHOD1(findHistogram,
                     absl::optional<std::reference_wrapper<const Histogram>>(StatName));

  Counter& counterFromStatName(StatName name) override {
    return counter(symbol_table_->toString(name));
  }
  Gauge& gaugeFromStatName(StatName name, Gauge::ImportMode import_mode) override {
    return gauge(symbol_table_->toString(name), import_mode);
  }
  Histogram& histogramFromStatName(StatName name) override {
    return histogram(symbol_table_->toString(name));
  }

  Test::Global<FakeSymbolTableImpl> symbol_table_;
  testing::NiceMock<MockCounter> counter_;
  std::vector<std::unique_ptr<MockHistogram>> histograms_;
};

/**
 * With IsolatedStoreImpl it's hard to test timing stats.
 * MockIsolatedStatsStore mocks only deliverHistogramToSinks for better testing.
 */
class MockIsolatedStatsStore : private Test::Global<Stats::FakeSymbolTableImpl>,
                               public IsolatedStoreImpl {
public:
  MockIsolatedStatsStore();
  ~MockIsolatedStatsStore();

  MOCK_METHOD2(deliverHistogramToSinks, void(const Histogram& histogram, uint64_t value));
};

class MockStatsMatcher : public StatsMatcher {
public:
  MockStatsMatcher();
  ~MockStatsMatcher();
  MOCK_CONST_METHOD1(rejects, bool(const std::string& name));
  bool acceptsAll() const override { return accepts_all_; }
  bool rejectsAll() const override { return rejects_all_; }

  bool accepts_all_{false};
  bool rejects_all_{false};
};

} // namespace Stats
} // namespace Envoy
