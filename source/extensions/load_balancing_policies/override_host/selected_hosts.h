#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace OverrideHost {

// The SelectedHosts struct is used to represent parsed proto or headers with
// the pre-selected primary and retry hosts.
// The structure is immutable and can only be created via the factory methods.
struct SelectedHosts {
  struct Endpoint {
    const std::string address_and_port;
  };

  const Endpoint primary;
  const std::vector<Endpoint> failover;

  static absl::StatusOr<std::unique_ptr<SelectedHosts>> make(absl::string_view selected_endpoints);
};

} // namespace OverrideHost
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
