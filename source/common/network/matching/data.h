#pragma once

#include "source/common/network/cidr_range.h"

namespace Envoy {
namespace Network {
namespace Matching {
class NetworkMatchingData {
public:
  static absl::string_view name() { return "network"; }

  virtual ~NetworkMatchingData() = default;

  virtual const Address::CidrRange& sourceIp() const PURE;
  virtual const Address::CidrRange& destinationIp() const PURE;
};
} // namespace Matching
} // namespace Network
} // namespace Envoy
