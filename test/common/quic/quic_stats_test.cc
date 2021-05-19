#include <string>

#include "common/quic/quic_stats.h"

#include "test/mocks/stats/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

class QuicStatsTest : public testing::Test {
public:
  QuicStatsTest() : scope_(*symbol_table_), quic_stats_(*symbol_table_) {}

  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::TestUtil::TestStore scope_;
  QuicStats quic_stats_;
};

TEST_F(QuicStatsTest, QuicConnectionCloseStats) {
  quic_stats_.chargeQuicConnectionCloseStats(scope_, quic::QUIC_NO_ERROR,
                                             quic::ConnectionCloseSource::FROM_SELF, false);
  EXPECT_EQ(
      1U, scope_.counter("downstream.self.quic_connection_close_error_code_QUIC_NO_ERROR").value());
}

} // namespace Quic
} // namespace Envoy
