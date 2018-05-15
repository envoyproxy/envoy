#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Stats {

MockCounter::MockCounter() {
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, tagExtractedName()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, tags()).WillByDefault(ReturnRef(tags_));
  ON_CALL(*this, used()).WillByDefault(ReturnPointee(&used_));
  ON_CALL(*this, value()).WillByDefault(ReturnPointee(&value_));
  ON_CALL(*this, latch()).WillByDefault(ReturnPointee(&latch_));
}
MockCounter::~MockCounter() {}

MockGauge::MockGauge() {
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, tagExtractedName()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, tags()).WillByDefault(ReturnRef(tags_));
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
  ON_CALL(*this, tagExtractedName()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, tags()).WillByDefault(ReturnRef(tags_));
}

MockHistogram::~MockHistogram() {}

MockParentHistogram::MockParentHistogram() {
  ON_CALL(*this, recordValue(_)).WillByDefault(Invoke([this](uint64_t value) {
    if (store_ != nullptr) {
      store_->deliverHistogramToSinks(*this, value);
    }
  }));
  ON_CALL(*this, tagExtractedName()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, tags()).WillByDefault(ReturnRef(tags_));
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

MockStore::MockStore() {
  ON_CALL(*this, counter(_)).WillByDefault(ReturnRef(counter_));
  ON_CALL(*this, histogram(_)).WillByDefault(Invoke([this](const std::string& name) -> Histogram& {
    auto* histogram = new NiceMock<MockHistogram>;
    histogram->name_ = name;
    histogram->store_ = this;
    histograms_.emplace_back(histogram);
    return *histogram;
  }));
}
MockStore::~MockStore() {}

MockIsolatedStatsStore::MockIsolatedStatsStore() {}
MockIsolatedStatsStore::~MockIsolatedStatsStore() {}

} // namespace Stats
} // namespace Envoy
