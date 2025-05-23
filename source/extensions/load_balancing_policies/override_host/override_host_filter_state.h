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

  OverrideHostFilterState(std::string&& host_list) : host_list_(std::move(host_list)) {}
  bool empty() const { return host_list_.empty(); }
  absl::string_view consumeNextHost() {
    for (; host_index_ < host_list_.size();) {
      auto host = absl::string_view(host_list_).substr(host_index_);
      host = host.substr(0, host.find(','));

      host_index_ += host.size() + 1; // +1 for the delimiter

      // Skip senseless host.
      if (host = absl::StripAsciiWhitespace(host); !host.empty()) {
        return host;
      }
    }
    return {};
  }

private:
  const std::string host_list_;
  uint64_t host_index_ = 0;
};

} // namespace OverrideHost
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
