#include "mocks.h"

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

} // Stats
