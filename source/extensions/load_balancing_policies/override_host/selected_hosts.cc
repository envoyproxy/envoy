#include "source/extensions/load_balancing_policies/override_host/selected_hosts.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/network/address.h"

#include "source/common/network/utility.h"
#include "source/extensions/load_balancing_policies/override_host/metadata_keys.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace DynamicForwarding {

namespace {
// This function parses address from the string representation of the address.
// The string representation has the format of "ip:port".
// For example: "1.2.3.4:80" or "[2001:db8::1]:80".
absl::StatusOr<SelectedHosts::Endpoint::Address> toAddress(absl::string_view address) {
  if (address.empty()) {
    return absl::InvalidArgumentError("Address is empty");
  }
  Network::Address::InstanceConstSharedPtr parsed_address =
      Network::Utility::parseInternetAddressAndPortNoThrow(std::string(address));

  if (!parsed_address || parsed_address->type() != Network::Address::Type::Ip ||
      !parsed_address->ip()) {
    return absl::InvalidArgumentError(
        fmt::format("Address '{}' is not in host:port format", address));
  }
  return SelectedHosts::Endpoint::Address{
      parsed_address->ip()->addressAsString(),
      parsed_address->ip()->port(),
  };
}

// This function parses list of endpoints from the string representation of the
// endpoints.
// The string representation has the format of "ip:port,ip:port,...".
// For example: "1.2.3.4:80,5.6.7.8:80" or "[2001:db8::1]:80,[2001:db8::2]:80".
absl::StatusOr<std::vector<SelectedHosts::Endpoint>> extractEndpoints(absl::string_view addresses) {
  std::vector<SelectedHosts::Endpoint> endpoints;
  if (!addresses.empty()) {
    std::vector<absl::string_view> addresses_and_ports = absl::StrSplit(addresses, ',');
    for (const auto& address_and_port : addresses_and_ports) {
      auto address_result = toAddress(
          absl::StripTrailingAsciiWhitespace(absl::StripLeadingAsciiWhitespace(address_and_port)));
      if (!address_result.ok()) {
        return address_result.status();
      }
      endpoints.push_back(SelectedHosts::Endpoint{
          std::move(address_result.value()),
      });
    }
  }
  return endpoints;
}

} // namespace

absl::StatusOr<std::unique_ptr<SelectedHosts>>
SelectedHosts::make(const Envoy::ProtobufWkt::Struct& selected_endpoints) {
  if (!selected_endpoints.fields().contains(kPrimaryEndpointHeaderName)) {
    return absl::InvalidArgumentError("Missing primary endpoint");
  }
  if (!selected_endpoints.fields().at(kPrimaryEndpointHeaderName).has_string_value()) {
    return absl::InvalidArgumentError("Primary endpoint is not a string");
  }
  auto primary_endpoint_result =
      extractEndpoints(selected_endpoints.fields().at(kPrimaryEndpointHeaderName).string_value());
  if (!primary_endpoint_result.ok()) {
    return primary_endpoint_result.status();
  }
  if (primary_endpoint_result.value().size() != 1) {
    return absl::InvalidArgumentError("Primary endpoint is not a single address");
  }
  auto selected_hosts = std::make_unique<SelectedHosts>(
      SelectedHosts{std::move(primary_endpoint_result.value().front()), {}});
  return selected_hosts;
}

} // namespace DynamicForwarding
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
