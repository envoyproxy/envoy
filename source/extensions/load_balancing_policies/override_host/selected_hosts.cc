#include "source/extensions/load_balancing_policies/override_host/selected_hosts.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace OverrideHost {

namespace {
// This function parses list of endpoints from the string representation of the
// endpoints.
// The string representation has the format of "ip:port,ip:port,...".
// For example: "1.2.3.4:80,5.6.7.8:80" or "[2001:db8::1]:80,[2001:db8::2]:80".
std::vector<SelectedHosts::Endpoint> extractEndpoints(absl::string_view addresses) {
  std::vector<SelectedHosts::Endpoint> endpoints;
  if (!addresses.empty()) {
    std::vector<absl::string_view> addresses_and_ports = absl::StrSplit(addresses, ',');
    for (const auto& address_and_port : addresses_and_ports) {
      absl::string_view address_result =
          absl::StripTrailingAsciiWhitespace(absl::StripLeadingAsciiWhitespace(address_and_port));
      endpoints.push_back(SelectedHosts::Endpoint{std::string(address_result)});
    }
  }
  return endpoints;
}

} // namespace

absl::StatusOr<std::unique_ptr<SelectedHosts>>
SelectedHosts::make(absl::string_view selected_endpoints) {
  std::vector<SelectedHosts::Endpoint> primary_endpoint_result =
      extractEndpoints(selected_endpoints);
  if (primary_endpoint_result.size() != 1) {
    return absl::InvalidArgumentError("Primary endpoint is not a single address");
  }
  auto selected_hosts = std::make_unique<SelectedHosts>(
      SelectedHosts{std::move(primary_endpoint_result.front()), {}});
  return selected_hosts;
}

} // namespace OverrideHost
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
