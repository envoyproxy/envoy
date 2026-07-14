#pragma once

#include "envoy/stream_info/filter_state.h"

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace OverrideHost {

/**
 * FilerState object for storing index of the last used fallback endpoint.
 */
class OverrideHostFilterState : public StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view kFilterStateKey =
      "envoy.extensions.load_balancing_policies.override_host.filter_state";

  OverrideHostFilterState(std::vector<std::string>&& host_list)
      : host_list_(std::move(host_list)) {}
  bool empty() const { return host_list_.empty(); }

  /**
   * @return consume next valid host from the list of selected hosts.
   * Empty string view is returned if there are no more hosts.
   */
  absl::string_view consumeNextHost() {
    if (host_index_ >= host_list_.size()) {
      return {};
    }
    return host_list_[host_index_++];
  }

private:
  const std::vector<std::string> host_list_;
  uint64_t host_index_ = 0;
};

} // namespace OverrideHost
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
