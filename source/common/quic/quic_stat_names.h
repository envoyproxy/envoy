#pragma once

#ifdef ENVOY_ENABLE_QUIC

#include "envoy/stats/scope.h"

#include "source/common/common/thread.h"
#include "source/common/stats/symbol_table.h"

#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"

namespace Envoy {
namespace Quic {

class QuicStatNames {
public:
  // This class holds lazily symbolized stat names and is responsible for charging them.
  explicit QuicStatNames(Stats::SymbolTable& symbol_table);

  void chargeQuicConnectionCloseStats(Stats::Scope& scope, quic::QuicErrorCode error_code,
                                      quic::ConnectionCloseSource source, bool is_upstream);

  void chargeQuicResetStreamErrorStats(Stats::Scope& scope, quic::QuicResetStreamError error_code,
                                       bool from_self, bool is_upstream);

private:
  // Find the actual counter in |scope| and increment it.
  // An example counter name: "http3.downstream.tx.quic_connection_close_error_code_QUIC_NO_ERROR".
  void incCounter(Stats::Scope& scope, const Stats::StatNameVec& names);

  Stats::StatName connectionCloseStatName(quic::QuicErrorCode error_code);

  Stats::StatName resetStreamErrorStatName(quic::QuicRstStreamErrorCode error_code);

  Stats::StatNamePool stat_name_pool_;
  Stats::SymbolTable& symbol_table_;
  const Stats::StatName http3_prefix_;
  const Stats::StatName downstream_;
  const Stats::StatName upstream_;
  const Stats::StatName from_self_;
  const Stats::StatName from_peer_;
  Thread::AtomicPtrArray<const uint8_t, quic::QUIC_LAST_ERROR + 1,
                         Thread::AtomicPtrAllocMode::DoNotDelete>
      connection_error_stat_names_;
  Thread::AtomicPtrArray<const uint8_t, quic::QUIC_STREAM_LAST_ERROR + 1,
                         Thread::AtomicPtrAllocMode::DoNotDelete>
      reset_stream_error_stat_names_;
};

#else

#include "source/common/stats/symbol_table.h"
namespace Envoy {
namespace Quic {

class QuicStatNames {
public:
  explicit QuicStatNames(Stats::SymbolTable& symbol_table);
};

#endif

} // namespace Quic
} // namespace Envoy
