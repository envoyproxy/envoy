#include "common/grpc/google_grpc_stat_names.h"

namespace Envoy {
namespace Grpc {

GoogleGrpcStatNames::GoogleGrpcStatNames(Stats::SymbolTable& symbol_table)
    : pool_(symbol_table), streams_total_(pool_.add("streams_total")) {
  for (uint32_t i = 0; i <= Status::WellKnownGrpcStatus::MaximumKnown; ++i) {
    streams_closed_[i] = pool_.add(absl::StrCat("streams_closed_", i));
  }
}

} // namespace Grpc
} // namespace Envoy
