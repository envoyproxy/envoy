#include "mocks.h"

#include <memory>

#include "source/common/stats/symbol_table.h"

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

MockCounter::MockCounter() {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, hidden()).WillByDefault(ReturnPointee(&hidden_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
  ON_CALL(*this, latch()).WillByDefault(ReturnPointee(&latch_));
}
MockCounter::~MockCounter() = default;

MockGauge::MockGauge() : used_(false), value_(0), import_mode_(ImportMode::Accumulate) {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, hidden()).WillByDefault(ReturnPointee(&hidden_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
  ON_CALL(*this, importMode()).WillByDefault(ReturnPointee(&import_mode_));
}
MockGauge::~MockGauge() = default;

MockTextReadout::MockTextReadout() {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, hidden()).WillByDefault(ReturnPointee(&hidden_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
}
MockTextReadout::~MockTextReadout() = default;

MockHistogram::MockHistogram() {
  ON_CALL(*this, unit()).WillByDefault(ReturnPointee(&unit_));
  ON_CALL(*this, recordValue(_)).WillByDefault(Invoke([this](uint64_t value) {
    if (store_ != nullptr) {
      store_->deliverHistogramToSinks(*this, value);
    }
  }));
}
MockHistogram::~MockHistogram() = default;

MockParentHistogram::MockParentHistogram() {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, hidden()).WillByDefault(ReturnPointee(&hidden_));
  ON_CALL(*this, unit()).WillByDefault(ReturnPointee(&unit_));
  ON_CALL(*this, recordValue(_)).WillByDefault(Invoke([this](uint64_t value) {
    if (store_ != nullptr) {
      store_->deliverHistogramToSinks(*this, value);
    }
  }));
  ON_CALL(*this, intervalStatistics()).WillByDefault(ReturnRef(*histogram_stats_));
  ON_CALL(*this, cumulativeStatistics()).WillByDefault(ReturnRef(*histogram_stats_));
}
MockParentHistogram::~MockParentHistogram() = default;

MockMetricSnapshot::MockMetricSnapshot() {
  ON_CALL(*this, counters()).WillByDefault(ReturnRef(counters_));
  ON_CALL(*this, gauges()).WillByDefault(ReturnRef(gauges_));
  ON_CALL(*this, histograms()).WillByDefault(ReturnRef(histograms_));
  ON_CALL(*this, hostCounters()).WillByDefault(ReturnRef(host_counters_));
  ON_CALL(*this, hostGauges()).WillByDefault(ReturnRef(host_gauges_));
  ON_CALL(*this, snapshotTime()).WillByDefault(Return(snapshot_time_));
}

MockMetricSnapshot::~MockMetricSnapshot() = default;

MockSink::MockSink() = default;
MockSink::~MockSink() = default;

MockSinkPredicates::MockSinkPredicates() = default;
MockSinkPredicates::~MockSinkPredicates() = default;

MockScope::MockScope(StatName prefix, MockStore& store)
    : TestUtil::TestScope(prefix, store), mock_store_(store) {}

Counter& MockScope::counterFromStatNameWithTags(const StatName& name,
                                                StatNameTagVectorOptConstRef) {
  // We always just respond with the mocked counter, so the tags don't matter.
  return mock_store_.counter(symbolTable().toString(name));
}
Gauge& MockScope::gaugeFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef,
                                            Gauge::ImportMode import_mode) {
  // We always just respond with the mocked gauge, so the tags don't matter.
  return mock_store_.gauge(symbolTable().toString(name), import_mode);
}
Histogram& MockScope::histogramFromStatNameWithTags(const StatName& name,
                                                    StatNameTagVectorOptConstRef,
                                                    Histogram::Unit unit) {
  return mock_store_.histogram(symbolTable().toString(name), unit);
}
TextReadout& MockScope::textReadoutFromStatNameWithTags(const StatName& name,
                                                        StatNameTagVectorOptConstRef) {
  // We always just respond with the mocked counter, so the tags don't matter.
  return mock_store_.textReadout(symbolTable().toString(name));
}

MockStore::MockStore() {
  ON_CALL(*this, counter(_)).WillByDefault(ReturnRef(counter_));
  ON_CALL(*this, gauge(_, _)).WillByDefault(ReturnRef(gauge_));
  ON_CALL(*this, histogram(_, _))
      .WillByDefault(Invoke([this](const std::string& name, Histogram::Unit unit) -> Histogram& {
        auto* histogram = new NiceMock<MockHistogram>(); // symbol_table_);
        histogram->name_ = name;
        histogram->unit_ = unit;
        histogram->store_ = this;
        histograms_.emplace_back(histogram);
        return *histogram;
      }));
}
MockStore::~MockStore() = default;

ScopeSharedPtr MockStore::makeScope(StatName prefix) {
  return std::make_shared<MockScope>(prefix, *this);
}

MockIsolatedStatsStore::MockIsolatedStatsStore() = default;
MockIsolatedStatsStore::~MockIsolatedStatsStore() = default;

MockStatsMatcher::MockStatsMatcher() = default;
MockStatsMatcher::~MockStatsMatcher() = default;

} // namespace Stats
} // namespace Envoy
