#include <string>

#include "common/quic/quic_stat_names.h"

#include "test/mocks/stats/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

class QuicStatNamesTest : public testing::Test {
public:
  QuicStatNamesTest() : scope_(*symbol_table_), quic_stat_names_(*symbol_table_) {}

  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::TestUtil::TestStore scope_;
  QuicStatNames quic_stat_names_;
};

TEST_F(QuicStatNamesTest, QuicConnectionCloseStats) {
  quic_stat_names_.chargeQuicConnectionCloseStats(scope_, quic::QUIC_NO_ERROR,
                                                  quic::ConnectionCloseSource::FROM_SELF, false);
  EXPECT_EQ(1U,
            scope_.counter("http3.downstream.self.quic_connection_close_error_code_QUIC_NO_ERROR")
                .value());
}

} // namespace Quic
} // namespace Envoy
