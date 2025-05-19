#pragma once

#include "envoy/stream_info/filter_state.h"

#include "absl/strings/str_split.h"
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

  OverrideHostFilterState(std::string&& host_list) : host_list_(std::move(host_list)) {
    for (absl::string_view host : absl::StrSplit(host_list_, ',', absl::SkipWhitespace())) {
      selected_hosts_.push_back(absl::StripAsciiWhitespace(host));
    }
  }

  uint64_t hostIndex() const { return host_index_; }
  void setHostIndex(uint64_t host_index) { host_index_ = host_index; }

  absl::Span<const absl::string_view> selectedHosts() const { return selected_hosts_; }

private:
  const std::string host_list_;
  absl::InlinedVector<const absl::string_view, 8> selected_hosts_;
  uint64_t host_index_ = 0;
};

} // namespace OverrideHost
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
