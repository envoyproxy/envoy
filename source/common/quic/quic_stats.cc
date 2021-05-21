#include "common/quic/quic_stats.h"

namespace Envoy {
namespace Quic {

QuicStats::QuicStats(Stats::Scope& scope, bool is_upstream)
    : scope_(scope), stat_name_pool_(scope.symbolTable()), symbol_table_(scope.symbolTable()),
      direction_(is_upstream ? stat_name_pool_.add("upstream") : stat_name_pool_.add("downstream")),
      from_self_(stat_name_pool_.add("self")), from_peer_(stat_name_pool_.add("peer")) {
  // Preallocate most used counters
  connectionCloseStatName(quic::QUIC_NETWORK_IDLE_TIMEOUT);
}

void QuicStats::incCounter(const Stats::StatNameVec& names) {
  Stats::SymbolTable::StoragePtr stat_name_storage = symbol_table_.join(names);
  scope_.counterFromStatName(Stats::StatName(stat_name_storage.get())).inc();
}

void QuicStats::chargeQuicConnectionCloseStats(quic::QuicErrorCode error_code,
                                               quic::ConnectionCloseSource source) {
  const Stats::StatName connection_close = connectionCloseStatName(error_code);
  incCounter({direction_,
              source == quic::ConnectionCloseSource::FROM_SELF ? from_self_ : from_peer_,
              connection_close});
}

Stats::StatName QuicStats::connectionCloseStatName(quic::QuicErrorCode error_code) {
  ASSERT(error_code < quic::QUIC_LAST_ERROR);

  return Stats::StatName(
      connection_error_stat_names_.get(error_code, [this, error_code]() -> const uint8_t* {
        return stat_name_pool_.addReturningStorage(
            absl::StrCat("quic_connection_close_error_code_", QuicErrorCodeToString(error_code)));
      }));
}

} // namespace Quic
} // namespace Envoy
