#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "source/common/protobuf/protobuf.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace DynamicForwarding {

// The SelectedHosts struct is used to represent parsed proto or headers with
// the pre-selected primary and retry hosts.
// The structure is immutable and can only be created via the factory methods.
struct SelectedHosts {
  struct Endpoint {
  public:
    struct Address {
      const std::string address;
      const uint32_t port;
    };

    const Address address;
  };

  const Endpoint primary;
  const std::vector<Endpoint> failover;

  static absl::StatusOr<std::unique_ptr<SelectedHosts>>
  make(const Envoy::ProtobufWkt::Struct& selected_endpoints);
};

} // namespace DynamicForwarding
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
