#pragma once

#include <string>

#include "envoy/stream_info/filter_state.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Router {

struct DebugConfig : public StreamInfo::FilterState::Object {
  static const std::string& key();

  bool append_cluster_{};
  absl::optional<std::string> cluster_header_;

  bool append_host_{};
  absl::optional<std::string> hostname_header_;
  absl::optional<std::string> host_address_header_;

  bool do_not_forward_{};
};

} // namespace Router
} // namespace Envoy
