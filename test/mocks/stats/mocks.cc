#include "mocks.h"

using testing::_;

namespace Stats {

MockCounter::MockCounter() {}
MockCounter::~MockCounter() {}

MockTimespan::MockTimespan() {}
MockTimespan::~MockTimespan() {}

MockStore::MockStore() { ON_CALL(*this, counter(_)).WillByDefault(ReturnRef(counter_)); }
MockStore::~MockStore() {}

MockIsolatedStatsStore::MockIsolatedStatsStore() {}
MockIsolatedStatsStore::~MockIsolatedStatsStore() {}

} // Stats
