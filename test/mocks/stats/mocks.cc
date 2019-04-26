#include "mocks.h"

#include <memory>

#include "common/stats/fake_symbol_table_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Stats {

MockMetric::MockMetric() : name_(*this) {}
MockMetric::~MockMetric() {}

MockMetric::MetricName::~MetricName() {
  if (stat_name_storage_ != nullptr) {
    stat_name_storage_->free(*mock_metric_.symbol_table_);
  }
}

void MockMetric::setTagExtractedName(absl::string_view name) {
  tag_extracted_name_ = std::string(name);
  tag_extracted_stat_name_ =
      std::make_unique<StatNameManagedStorage>(tagExtractedName(), *symbol_table_);
}

void MockMetric::MetricName::MetricName::operator=(absl::string_view name) {
  name_ = std::string(name);
  stat_name_storage_ = std::make_unique<StatNameStorage>(name, mock_metric_.symbolTable());
}

MockCounter::MockCounter() {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
  ON_CALL(*this, latch()).WillByDefault(ReturnPointee(&latch_));
}
MockCounter::~MockCounter() {}

MockGauge::MockGauge() {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
}
MockGauge::~MockGauge() {}

MockHistogram::MockHistogram() {
  ON_CALL(*this, recordValue(_)).WillByDefault(Invoke([this](uint64_t value) {
    if (store_ != nullptr) {
      store_->deliverHistogramToSinks(*this, value);
    }
  }));
}
MockHistogram::~MockHistogram() {}

MockParentHistogram::MockParentHistogram() {
  ON_CALL(*this, recordValue(_)).WillByDefault(Invoke([this](uint64_t value) {
    if (store_ != nullptr) {
      store_->deliverHistogramToSinks(*this, value);
    }
  }));
  ON_CALL(*this, intervalStatistics()).WillByDefault(ReturnRef(*histogram_stats_));
  ON_CALL(*this, cumulativeStatistics()).WillByDefault(ReturnRef(*histogram_stats_));
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
}
MockParentHistogram::~MockParentHistogram() {}

MockSource::MockSource() {
  ON_CALL(*this, cachedCounters()).WillByDefault(ReturnRef(counters_));
  ON_CALL(*this, cachedGauges()).WillByDefault(ReturnRef(gauges_));
  ON_CALL(*this, cachedHistograms()).WillByDefault(ReturnRef(histograms_));
}

MockSource::~MockSource() {}

MockSink::MockSink() {}
MockSink::~MockSink() {}

MockStore::MockStore() : StoreImpl(*fake_symbol_table_) {
  ON_CALL(*this, counter(_)).WillByDefault(ReturnRef(counter_));
  ON_CALL(*this, histogram(_)).WillByDefault(Invoke([this](const std::string& name) -> Histogram& {
    auto* histogram = new NiceMock<MockHistogram>(); // symbol_table_);
    histogram->name_ = name;
    histogram->store_ = this;
    histograms_.emplace_back(histogram);
    return *histogram;
  }));
  ON_CALL(*this, statsOptions()).WillByDefault(ReturnRef(stats_options_));
}
MockStore::~MockStore() {}

MockIsolatedStatsStore::MockIsolatedStatsStore()
    : IsolatedStoreImpl(Test::Global<Stats::FakeSymbolTableImpl>::get()) {}
MockIsolatedStatsStore::~MockIsolatedStatsStore() {}

MockStatsMatcher::MockStatsMatcher() {}
MockStatsMatcher::~MockStatsMatcher() {}

} // namespace Stats
} // namespace Envoy
