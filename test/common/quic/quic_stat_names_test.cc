#include <string>

#include "source/common/quic/quic_stat_names.h"

#include "test/mocks/stats/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

class QuicStatNamesTest : public testing::Test {
public:
  QuicStatNamesTest()
      : store_(*symbol_table_), scope_(*store_.rootScope()), quic_stat_names_(*symbol_table_) {}

  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::TestUtil::TestStore store_;
  Stats::Scope& scope_;
  QuicStatNames quic_stat_names_;
};

TEST_F(QuicStatNamesTest, QuicConnectionCloseStats) {
  quic_stat_names_.chargeQuicConnectionCloseStats(scope_, quic::QUIC_NO_ERROR,
                                                  quic::ConnectionCloseSource::FROM_SELF, false);
  EXPECT_EQ(
      1U,
      store_.counter("http3.downstream.tx.quic_connection_close_error_code_QUIC_NO_ERROR").value());
}

TEST_F(QuicStatNamesTest, OutOfRangeQuicConnectionCloseStats) {
  uint64_t bad_error_code = quic::QUIC_LAST_ERROR + 1;
  quic_stat_names_.chargeQuicConnectionCloseStats(scope_,
                                                  static_cast<quic::QuicErrorCode>(bad_error_code),
                                                  quic::ConnectionCloseSource::FROM_SELF, false);
  EXPECT_EQ(1U,
            store_.counter("http3.downstream.tx.quic_connection_close_error_code_QUIC_LAST_ERROR")
                .value());
}

TEST_F(QuicStatNamesTest, ResetStreamErrorStats) {
  quic_stat_names_.chargeQuicResetStreamErrorStats(
      scope_, quic::QuicResetStreamError::FromInternal(quic::QUIC_STREAM_CANCELLED), true, false);
  EXPECT_EQ(1U,
            store_.counter("http3.downstream.tx.quic_reset_stream_error_code_QUIC_STREAM_CANCELLED")
                .value());
}

TEST_F(QuicStatNamesTest, OutOfRangeResetStreamErrorStats) {
  uint64_t bad_error_code = quic::QUIC_STREAM_LAST_ERROR + 1;
  quic_stat_names_.chargeQuicResetStreamErrorStats(
      scope_,
      quic::QuicResetStreamError::FromInternal(
          static_cast<quic::QuicRstStreamErrorCode>(bad_error_code)),
      true, false);
  EXPECT_EQ(
      1U, store_.counter("http3.downstream.tx.quic_reset_stream_error_code_QUIC_STREAM_LAST_ERROR")
              .value());
}

} // namespace Quic
} // namespace Envoy
