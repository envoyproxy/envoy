#include "source/common/quic/quic_stat_names.h"

namespace Envoy {
namespace Quic {

// TODO(renjietang): Currently these stats are only available in downstream. Wire it up to upstream
// QUIC also.
QuicStatNames::QuicStatNames(Stats::SymbolTable& symbol_table)
    : stat_name_pool_(symbol_table), symbol_table_(symbol_table),
      http3_prefix_(stat_name_pool_.add("http3")), downstream_(stat_name_pool_.add("downstream")),
      upstream_(stat_name_pool_.add("upstream")), from_self_(stat_name_pool_.add("tx")),
      from_peer_(stat_name_pool_.add("rx")) {
  // Preallocate most used counters
  // Most popular in client initiated connection close.
  connectionCloseStatName(quic::QUIC_NETWORK_IDLE_TIMEOUT);
  // Most popular in server initiated connection close.
  connectionCloseStatName(quic::QUIC_SILENT_IDLE_TIMEOUT);
}

void QuicStatNames::incCounter(Stats::Scope& scope, const Stats::StatNameVec& names) {
  Stats::SymbolTable::StoragePtr stat_name_storage = symbol_table_.join(names);
  scope.counterFromStatName(Stats::StatName(stat_name_storage.get())).inc();
}

void QuicStatNames::chargeQuicConnectionCloseStats(Stats::Scope& scope,
                                                   quic::QuicErrorCode error_code,
                                                   quic::ConnectionCloseSource source,
                                                   bool is_upstream) {
  ASSERT(&symbol_table_ == &scope.symbolTable());

  if (error_code > quic::QUIC_LAST_ERROR) {
    error_code = quic::QUIC_LAST_ERROR;
  }

  const Stats::StatName connection_close = connectionCloseStatName(error_code);
  incCounter(scope, {http3_prefix_, is_upstream ? upstream_ : downstream_,
                     source == quic::ConnectionCloseSource::FROM_SELF ? from_self_ : from_peer_,
                     connection_close});
}

Stats::StatName QuicStatNames::connectionCloseStatName(quic::QuicErrorCode error_code) {
  ASSERT(error_code <= quic::QUIC_LAST_ERROR);

  return Stats::StatName(
      connection_error_stat_names_.get(error_code, [this, error_code]() -> const uint8_t* {
        return stat_name_pool_.addReturningStorage(
            absl::StrCat("quic_connection_close_error_code_", QuicErrorCodeToString(error_code)));
      }));
}

} // namespace Quic
} // namespace Envoy
