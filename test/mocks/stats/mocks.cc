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

MockMetric::MockMetric() { ON_CALL(*this, name()).WillByDefault(ReturnRef(name_)); }

MockMetric::~MockMetric() {}

MockCounter::MockCounter() {}
MockCounter::~MockCounter() {}

MockGauge::MockGauge() {}
MockGauge::~MockGauge() {}

MockHistogram::MockHistogram() {
  ON_CALL(*this, recordValue(_)).WillByDefault(Invoke([this](uint64_t value) {
    if (store_ != nullptr) {
      store_->deliverHistogramToSinks(*this, value);
    }
  }));
  ON_CALL(*this, type()).WillByDefault(ReturnPointee(&type_));
}
MockHistogram::~MockHistogram() {}

MockSink::MockSink() {}
MockSink::~MockSink() {}

MockStore::MockStore() {
  ON_CALL(*this, counter(_)).WillByDefault(ReturnRef(counter_));
  ON_CALL(*this, histogram(_, _))
      .WillByDefault(
          Invoke([this](Histogram::ValueType type, const std::string& name) -> Histogram& {
            auto* histogram = new NiceMock<MockHistogram>;
            histogram->name_ = name;
            histogram->store_ = this;
            histogram->type_ = type;
            histograms_.emplace_back(histogram);
            return *histogram;
          }));
}
MockStore::~MockStore() {}

MockIsolatedStatsStore::MockIsolatedStatsStore() {}
MockIsolatedStatsStore::~MockIsolatedStatsStore() {}

} // namespace Stats
} // namespace Envoy
