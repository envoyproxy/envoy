#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace OverrideHost {

// The dynamic forwarding load balancer extension stores the index of the
// fallback endpoint in the request metadata under this key.
// If this metadata is not present, the primary endpoint is used.
constexpr absl::string_view kEndpointsFallbackIndexKey =
    "envoy.extensions.load_balancing_policies.override_host.fallback_index";
constexpr absl::string_view kEndpointsFallbackIndexFieldName = "index";

} // namespace OverrideHost
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
