#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::_;

namespace Stats {

MockCounter::MockCounter() {}
MockCounter::~MockCounter() {}

MockGauge::MockGauge() {}
MockGauge::~MockGauge() {}

MockTimespan::MockTimespan() {}
MockTimespan::~MockTimespan() {}

MockSink::MockSink() {}
MockSink::~MockSink() {}

MockStore::MockStore() { ON_CALL(*this, counter(_)).WillByDefault(ReturnRef(counter_)); }
MockStore::~MockStore() {}

MockIsolatedStatsStore::MockIsolatedStatsStore() {}
MockIsolatedStatsStore::~MockIsolatedStatsStore() {}

} // namespace Stats
} // namespace Envoy
