#pragma once

#include <memory>

#include "envoy/type/matcher/v3/address.pb.h"

#include "source/common/network/cidr_range.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Matcher {

/**
 * Matches an IP address against a list of CIDR ranges, with optional inversion.
 * This is the C++ implementation of the envoy.type.matcher.v3.AddressMatcher proto.
 */
class AddressMatcher {
public:
  /**
   * Creates an AddressMatcher from the proto configuration.
   */
  static absl::StatusOr<std::unique_ptr<AddressMatcher>>
  create(const envoy::type::matcher::v3::AddressMatcher& matcher);

  AddressMatcher(std::unique_ptr<Network::Address::IpList>&& ip_list, bool invert_match = false);

  /**
   * Returns true if the address matches the configured CIDR ranges (respecting invert_match).
   */
  bool match(const Network::Address::Instance& address) const;

private:
  const std::unique_ptr<Network::Address::IpList> ip_list_;
  const bool invert_match_;
};

using AddressMatcherPtr = std::unique_ptr<AddressMatcher>;

} // namespace Matcher
} // namespace Envoy
