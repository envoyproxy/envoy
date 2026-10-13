#pragma once

#include "envoy/grpc/status.h"

#include "source/common/stats/symbol_table.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Grpc {

/**
 * Captures symbolized representation for tokens used in grpc stats. These are
 * broken out so they can be allocated early and used across all gRPC-related
 * filters.
 */
struct StatNames {
  explicit StatNames(Stats::SymbolTable& symbol_table);

  Stats::StatNamePool pool_;
  Stats::StatName streams_total_;
  std::array<Stats::StatName, Status::WellKnownGrpcStatus::MaximumKnown + 1> streams_closed_;
  absl::flat_hash_map<std::string, Stats::StatName> status_names_;
  // Stat name tracking the creation of the Google grpc client.
  Stats::StatName google_grpc_client_creation_;
};

} // namespace Grpc
} // namespace Envoy
