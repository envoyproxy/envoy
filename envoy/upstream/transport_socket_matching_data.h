#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Upstream {

/**
 * Data structure holding context for transport socket matching.
 * This provides access to:
 * - Endpoint metadata: metadata associated with the selected upstream endpoint.
 * - Locality metadata: metadata associated with the endpoint's locality.
 * - Filter state: shared filter state from downstream connection (via TransportSocketOptions).
 *
 * Filter state enables downstream-connection-based matching by allowing filters to explicitly
 * pass any data (e.g., network namespace, custom attributes) from downstream to upstream.
 * This follows the same pattern as tunneling in Envoy.
 */
struct TransportSocketMatchingData {
  static absl::string_view name() { return "transport_socket"; }

  TransportSocketMatchingData(const envoy::config::core::v3::Metadata* endpoint_metadata,
                              const envoy::config::core::v3::Metadata* locality_metadata,
                              const StreamInfo::FilterState* filter_state = nullptr)
      : endpoint_metadata_(endpoint_metadata), locality_metadata_(locality_metadata),
        filter_state_(filter_state) {}

  const envoy::config::core::v3::Metadata* endpoint_metadata_;
  const envoy::config::core::v3::Metadata* locality_metadata_;
  const StreamInfo::FilterState* filter_state_;
};

} // namespace Upstream
} // namespace Envoy
