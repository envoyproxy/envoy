#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace DynamicForwarding {

// This key name is currently used by OSS proposal.
// https://github.com/kubernetes-sigs/gateway-api-inference-extension/tree/main/docs/proposals/004-endpoint-picker-protocol
constexpr absl::string_view kSelectedEndpointsKey = "envoy.lb";

// The dynamic forwarding load balancer extension stores the index of the
// fallback endpoint in the request metadata under this key.
// If this metadata is not present, the primary endpoint is used.
constexpr absl::string_view kEndpointsFallbackIndexKey =
    "envoy.extensions.load_balancing_policies.dynamic_forwarding.fallback_index";
constexpr absl::string_view kEndpointsFallbackIndexFieldName = "index";

// This message can be represented as a protobuf Struct using the following
// schema:
// {
//   "x-gateway-destination-endpoint": {
//     "address": <string>,
//   },
//   "x-gateway-destination-endpoint-fallback": {
//   }
// }

// Default header names for providing the primary and fallback endpoints.
constexpr absl::string_view kPrimaryEndpointHeaderName = "x-gateway-destination-endpoint";
constexpr absl::string_view kFallbackSingleEndpointHeaderName =
    "x-gateway-destination-endpoint-fallback";
constexpr absl::string_view kFallbackEndpointsHeaderName =
    "x-gateway-destination-endpoint-fallbacks";

} // namespace DynamicForwarding
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
