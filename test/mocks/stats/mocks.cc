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

MockMetric::MockMetric() { ON_CALL(*this, name()).WillByDefault(ReturnPointee(&name_)); }

MockMetric::~MockMetric() {}

MockCounter::MockCounter() {}
MockCounter::~MockCounter() {}

MockGauge::MockGauge() {}
MockGauge::~MockGauge() {}

MockTimespan::MockTimespan() {}
MockTimespan::~MockTimespan() {}

MockTimer::MockTimer() {
  ON_CALL(*this, recordDuration(_)).WillByDefault(Invoke([this](std::chrono::milliseconds ms) {
    if (store_ != nullptr) {
      store_->deliverTimingToSinks(*this, ms);
    }
  }));
}
MockTimer::~MockTimer() {}

MockSink::MockSink() {}
MockSink::~MockSink() {}

MockStore::MockStore() {
  ON_CALL(*this, counter(_)).WillByDefault(ReturnRef(counter_));
  ON_CALL(*this, timer(_)).WillByDefault(Invoke([this](const std::string& name) -> Timer& {
    auto* timer = new NiceMock<MockTimer>;
    timer->name_ = name;
    timer->store_ = this;
    timers_.emplace_back(timer);
    return *timer;
  }));
}
MockStore::~MockStore() {}

MockIsolatedStatsStore::MockIsolatedStatsStore() {}
MockIsolatedStatsStore::~MockIsolatedStatsStore() {}

} // namespace Stats
} // namespace Envoy
