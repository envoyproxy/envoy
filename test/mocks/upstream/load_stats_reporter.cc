#include "test/mocks/upstream/load_stats_reporter.h"

namespace Envoy {
namespace Upstream {

MockLoadStatsReporter::MockLoadStatsReporter()
    : stats_{ALL_LOAD_REPORTER_STATS(POOL_COUNTER_PREFIX(*store_.rootScope(), "load_reporter."))} {
  ON_CALL(*this, getStats()).WillByDefault(testing::ReturnRef(stats_));
}

MockLoadStatsReporter::~MockLoadStatsReporter() = default;

} // namespace Upstream
} // namespace Envoy
