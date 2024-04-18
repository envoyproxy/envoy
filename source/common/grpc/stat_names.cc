#include "source/common/grpc/stat_names.h"

namespace Envoy {
namespace Grpc {

StatNames::StatNames(Stats::SymbolTable& symbol_table)
    : pool_(symbol_table), streams_total_(pool_.add("streams_total")),
      google_grpc_client_creation_(pool_.add("google_grpc_client_creation")) {
  for (uint32_t i = 0; i <= Status::WellKnownGrpcStatus::MaximumKnown; ++i) {
    std::string status_str = absl::StrCat(i);
    streams_closed_[i] = pool_.add(absl::StrCat("streams_closed_", status_str));
    status_names_[status_str] = pool_.add(status_str);
  }
}

} // namespace Grpc
} // namespace Envoy
