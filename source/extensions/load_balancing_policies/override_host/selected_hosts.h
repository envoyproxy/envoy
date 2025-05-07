#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/network/address.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace OverrideHost {

// The SelectedHosts struct is used to represent parsed proto or headers with
// the pre-selected primary and retry hosts.
// The structure is immutable and can only be created via the factory methods.
struct SelectedHosts {
  const Network::Address::InstanceConstSharedPtr primary;
  const std::vector<Network::Address::InstanceConstSharedPtr> failover;

  static absl::StatusOr<std::unique_ptr<SelectedHosts>> make(absl::string_view selected_endpoints);
};

} // namespace OverrideHost
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
