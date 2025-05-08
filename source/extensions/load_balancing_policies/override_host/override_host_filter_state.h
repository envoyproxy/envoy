#pragma once

#include "envoy/stream_info/filter_state.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace OverrideHost {

/**
 * FilerState object for storing index of the last used fallback endpoint.
 */
class OverrideHostFilterState : public StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view kFilterStateKey =
      "envoy.extensions.load_balancing_policies.override_host.filter_state";
  OverrideHostFilterState() = default;

  uint64_t fallbackHostIndex() const { return fallback_host_index_; }
  void setFallbackHostIndex(uint64_t fallback_host_index) {
    fallback_host_index_ = fallback_host_index;
  }

private:
  uint64_t fallback_host_index_ = 1;
};

} // namespace OverrideHost
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
