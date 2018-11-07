#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/stats/histogram.h"
#include "envoy/stats/sink.h"
#include "envoy/stats/source.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/store.h"
#include "envoy/stats/timespan.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/stats/histogram_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Stats {

class MockMetric : public virtual Metric {
public:
  explicit MockMetric(SymbolTable& symbol_table);
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

  SymbolTable& symbolTable() override { return *symbol_table_; }
  const SymbolTable& symbolTable() const override { return *symbol_table_; }

  // Note: cannot be mocked because it is accessed as a Property in a gmock EXPECT_CALL. This
  // creates a deadlock in gmock and is an unintended use of mock functions.
  std::string name() const override { return name_.name(); }
  StatName statName() const override { return name_.statName(); }
  // MOCK_CONST_METHOD0(tags, std::vector<Tag>());
  std::vector<Tag> tags() const override { return tags_; }
  std::string tagExtractedName() const override {
    return tag_extracted_name_.empty() ? name() : tag_extracted_name_;
  }
  // MOCK_CONST_METHOD0(tagExtractedName, std::string());

  SymbolTable* symbol_table_;
  MetricName name_;
  std::string tag_extracted_name_;
  std::vector<Tag> tags_;
};

class MockCounter : public Counter, public MockMetric {
public:
  MockCounter();
  explicit MockCounter(SymbolTable& symbol_table);
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
  explicit MockGauge(SymbolTable& symbol_table);
  ~MockGauge();

  MOCK_METHOD1(add, void(uint64_t amount));
  MOCK_METHOD0(dec, void());
  MOCK_METHOD0(inc, void());
  MOCK_METHOD1(set, void(uint64_t value));
  MOCK_METHOD1(sub, void(uint64_t amount));
  MOCK_CONST_METHOD0(used, bool());
  MOCK_CONST_METHOD0(value, uint64_t());

  bool used_;
  uint64_t value_;
};

class MockHistogram : public Histogram, public MockMetric {
public:
  MockHistogram();
  explicit MockHistogram(SymbolTable& symbol_table);
  ~MockHistogram();

  MOCK_METHOD1(recordValue, void(uint64_t value));
  MOCK_CONST_METHOD0(used, bool());

  Store* store_;
};

class MockParentHistogram : public ParentHistogram, public MockMetric {
public:
  MockParentHistogram();
  explicit MockParentHistogram(SymbolTable& symbol_table);
  ~MockParentHistogram();

  void merge() override {}
  const std::string summary() const override { return ""; };

  MOCK_CONST_METHOD0(used, bool());
  MOCK_METHOD1(recordValue, void(uint64_t value));
  MOCK_CONST_METHOD0(cumulativeStatistics, const HistogramStatistics&());
  MOCK_CONST_METHOD0(intervalStatistics, const HistogramStatistics&());

  bool used_;
  Store* store_;
  std::shared_ptr<HistogramStatistics> histogram_stats_ =
      std::make_shared<HistogramStatisticsImpl>();
};

class MockSource : public Source {
public:
  MockSource();
  ~MockSource();

  MOCK_METHOD0(cachedCounters, const std::vector<CounterSharedPtr>&());
  MOCK_METHOD0(cachedGauges, const std::vector<GaugeSharedPtr>&());
  MOCK_METHOD0(cachedHistograms, const std::vector<ParentHistogramSharedPtr>&());
  MOCK_METHOD0(clearCache, void());

  std::vector<CounterSharedPtr> counters_;
  std::vector<GaugeSharedPtr> gauges_;
  std::vector<ParentHistogramSharedPtr> histograms_;
};

class MockSink : public Sink {
public:
  MockSink();
  ~MockSink();

  MOCK_METHOD1(flush, void(Source& source));
  MOCK_METHOD2(onHistogramComplete, void(const Histogram& histogram, uint64_t value));
};

class MockStore : public Store {
public:
  MockStore();
  ~MockStore();

  ScopePtr createScope(const std::string& name) override { return ScopePtr{createScope_(name)}; }

  MOCK_METHOD2(deliverHistogramToSinks, void(const Histogram& histogram, uint64_t value));
  MOCK_METHOD1(counter, Counter&(const std::string&));
  MOCK_METHOD1(counterx, Counter&(StatName));
  MOCK_CONST_METHOD0(counters, std::vector<CounterSharedPtr>());
  MOCK_METHOD1(createScope_, Scope*(const std::string& name));
  MOCK_METHOD1(gauge, Gauge&(const std::string&));
  MOCK_METHOD1(gaugex, Gauge&(StatName));
  MOCK_CONST_METHOD0(gauges, std::vector<GaugeSharedPtr>());
  MOCK_METHOD1(histogram, Histogram&(const std::string& name));
  MOCK_METHOD1(histogramx, Histogram&(StatName));
  MOCK_CONST_METHOD0(histograms, std::vector<ParentHistogramSharedPtr>());
  MOCK_CONST_METHOD0(statsOptions, const StatsOptions&());

  SymbolTable& symbolTable() override { return symbol_table_; }
  const SymbolTable& symbolTable() const override { return symbol_table_; }

  SymbolTable symbol_table_;
  testing::NiceMock<MockCounter> counter_;
  std::vector<std::unique_ptr<MockHistogram>> histograms_;
  StatsOptionsImpl stats_options_;
};

/**
 * With IsolatedStoreImpl it's hard to test timing stats.
 * MockIsolatedStatsStore mocks only deliverHistogramToSinks for better testing.
 */
class MockIsolatedStatsStore : public IsolatedStoreImpl {
public:
  MockIsolatedStatsStore();
  ~MockIsolatedStatsStore();

  MOCK_METHOD2(deliverHistogramToSinks, void(const Histogram& histogram, uint64_t value));
};

} // namespace Stats
} // namespace Envoy
