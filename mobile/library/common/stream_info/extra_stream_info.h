#pragma once

#include "envoy/stream_info/filter_state.h"

#include "source/common/stream_info/utility.h"

#include "library/common/network/configurator.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace StreamInfo {

struct ExtraStreamInfo : public FilterState::Object {
  absl::optional<envoy_netconf_t> configuration_key_{};
  static const std::string& key();
};

// Set fields in final_intel based on stream_info.
void setFinalStreamIntel(StreamInfo& stream_info, envoy_final_stream_intel& final_intel);

// Returns true if the response code details indicate that this stream info
// has a stream idle timeout error.
bool isStreamIdleTimeout(const StreamInfo& stream_info);

using ExtraStreamInfoPtr = std::unique_ptr<ExtraStreamInfo>;

} // namespace StreamInfo
} // namespace Envoy
