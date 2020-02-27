#pragma once

#include "envoy/grpc/status.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Grpc {

struct GoogleGrpcStatNames {
  GoogleGrpcStatNames(Stats::SymbolTable& symbol_table);

  Stats::StatNamePool pool_;
  Stats::StatName streams_total_;
  std::array<Stats::StatName, Status::WellKnownGrpcStatus::MaximumKnown + 1> streams_closed_;
};

} // namespace Grpc
} // namespace Envoy
