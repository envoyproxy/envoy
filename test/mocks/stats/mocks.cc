#include "mocks.h"

#include <memory>

#include "common/stats/fake_symbol_table_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Stats {

MockCounter::MockCounter() {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
  ON_CALL(*this, latch()).WillByDefault(ReturnPointee(&latch_));
}
MockCounter::~MockCounter() = default;

MockGauge::MockGauge() : used_(false), value_(0), import_mode_(ImportMode::Accumulate) {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
  ON_CALL(*this, importMode()).WillByDefault(ReturnPointee(&import_mode_));
}
MockGauge::~MockGauge() = default;

MockTextReadout::MockTextReadout() {
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
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
}

MockMetricSnapshot::~MockMetricSnapshot() = default;

MockSink::MockSink() = default;
MockSink::~MockSink() = default;

MockStore::MockStore() : TestUtil::TestStore(*global_symbol_table_) {
  ON_CALL(*this, counter(_)).WillByDefault(ReturnRef(counter_));
  ON_CALL(*this, histogram(_, _))
      .WillByDefault(Invoke([this](const std::string& name, Histogram::Unit unit) -> Histogram& {
        auto* histogram = new NiceMock<MockHistogram>(); // symbol_table_);
        histogram->name_ = name;
        histogram->unit_ = unit;
        histogram->store_ = this;
        histograms_.emplace_back(histogram);
        return *histogram;
      }));

  ON_CALL(*this, histogramFromString(_, _))
      .WillByDefault(Invoke([this](const std::string& name, Histogram::Unit unit) -> Histogram& {
        return TestUtil::TestStore::histogramFromString(name, unit);
      }));
}
MockStore::~MockStore() = default;

MockIsolatedStatsStore::MockIsolatedStatsStore() : TestUtil::TestStore(*global_symbol_table_) {}
MockIsolatedStatsStore::~MockIsolatedStatsStore() = default;

MockStatsMatcher::MockStatsMatcher() = default;
MockStatsMatcher::~MockStatsMatcher() = default;

} // namespace Stats
} // namespace Envoy
