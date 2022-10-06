#pragma once

#include "envoy/network/address.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Network {

struct LookupResult {
  // Returns the resolved addresses from A and AAAA records. It is left to the
  // user to sort out which address families are supported.
  std::vector<Network::Address::InstanceConstSharedPtr> resolved_addresses;
};

// Does a DNS lookup, and returns the LookupResult with any supported resource records or an error.
absl::StatusOr<LookupResult> doSrvLookup(absl::string_view hostname, int32_t port);

} // namespace Network
} // namespace Envoy
