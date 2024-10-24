#include "source/extensions/quic/connection_debug_visitor/quic_stats/quic_stats.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/listener_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Quic {
namespace ConnectionDebugVisitors {
namespace QuicStats {

// Variant of QuicStatsVisitor for testing that has it's own stats, isntead of requiring mocks
// for several quiche objects.
class TestQuicStatsVisitor : public QuicStatsVisitor {
public:
  using QuicStatsVisitor::QuicStatsVisitor;

  const quic::QuicConnectionStats& getQuicStats() override { return stats_; }

  quic::QuicConnectionStats stats_;
};

/*class MockQuicSession : public quic::QuicSession {
public:
  MockQuicSession() : quic::QuicSession() {}
  bool ShouldKeepConnectionAlive() const override { return false; }
  const quic::QuicCryptoStream* GetCryptoStream() const override { return nullptr; }
  quic::QuicStream* CreateIncomingStream(quic::QuicStreamId) override { return nullptr; }
  quic::QuicStream* CreateIncomingStream(quic::PendingStream*) override { return nullptr; }
  quic::QuicCryptoStream* GetMutableCryptoStream() override { return nullptr; }
  };*/

class QuicStatsTest : public testing::Test {
public:
  QuicStatsTest()
      : quic_stats_(std::make_unique<TestQuicStatsVisitor>(
            *config_, context_.server_factory_context_.dispatcher_, nullptr)) {}
  // MockQuicSession session_;
  Stats::TestUtil::TestStore store_;
  NiceMock<Event::MockTimer>* timer_;
  Server::Configuration::MockListenerFactoryContext context_;
  std::unique_ptr<Config> config_;
  std::unique_ptr<TestQuicStatsVisitor> quic_stats_;
};

TEST_F(QuicStatsTest, blah) { EXPECT_TRUE(false); }

} // namespace QuicStats
} // namespace ConnectionDebugVisitors
} // namespace Quic
} // namespace Extensions
} // namespace Envoy
