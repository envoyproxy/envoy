#include "source/extensions/quic/connection_debug_visitor/quic_stats/quic_stats.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Quic {
namespace ConnectionDebugVisitors {
namespace QuicStats {

class QuicStatsTest : public testing::Test {
public:
  Stats::TestUtil::TestStore store_;
  NiceMock<Event::MockTimer>* timer_;
  std::unique_ptr<QuicStatsVisitor> quic_stats_;
};

} // namespace QuicStats
} // namespace ConnectionDebugVisitors
} // namespace Quic
} // namespace Extensions
} // namespace Envoy
