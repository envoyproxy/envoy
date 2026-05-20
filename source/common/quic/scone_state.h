#pragma once

#include <cstdint>

#include "envoy/stream_info/filter_state.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Quic {

// This struct stores the latest SCONE maximum bitrate received from the network.
// It's designed to be stored in the FilterState.
struct SconeState : public StreamInfo::FilterState::Object {
  ~SconeState() override;
  absl::optional<int64_t> scone_max_kbps;
  absl::optional<int64_t> timestamp_ms;
};

// Unique key to access SconeState within the FilterState.
constexpr absl::string_view SconeStateKey = "envoy.quic.scone_state";

} // namespace Quic
} // namespace Envoy
