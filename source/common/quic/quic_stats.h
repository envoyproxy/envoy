#pragma once

#include "envoy/stats/scope.h"

#include "common/common/thread.h"
#include "common/stats/symbol_table_impl.h"

#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"

namespace Envoy {
namespace Quic {

// This class is responsible for logging important QUIC error codes and tracking the dynamically
// created stat tokens.
class QuicStats {
public:
  QuicStats(Stats::Scope& scope, bool is_upstream);

  void chargeQuicConnectionCloseStats(quic::QuicErrorCode error_code,
                                      quic::ConnectionCloseSource source);

private:
  void incCounter(const Stats::StatNameVec& names);

  Stats::StatName connectionCloseStatName(quic::QuicErrorCode error_code);

  Stats::Scope& scope_;
  Stats::StatNamePool stat_name_pool_;
  Stats::SymbolTable& symbol_table_;
  const Stats::StatName direction_;
  const Stats::StatName from_self_;
  const Stats::StatName from_peer_;
  Thread::AtomicPtrArray<const uint8_t, quic::QUIC_LAST_ERROR,
                         Thread::AtomicPtrAllocMode::DoNotDelete>
      connection_error_stat_names_;
};

} // namespace Quic
} // namespace Envoy
