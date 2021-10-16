#pragma once

#include "envoy/stream_info/filter_state.h"

#include "library/common/network/configurator.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace StreamInfo {

struct ExtraStreamInfo : public FilterState::Object {
  absl::optional<envoy_netconf_t> configuration_key_{};
  static const std::string& key();
};

using ExtraStreamInfoPtr = std::unique_ptr<ExtraStreamInfo>;

} // namespace StreamInfo
} // namespace Envoy
