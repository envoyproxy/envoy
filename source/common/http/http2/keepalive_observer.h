#pragma once

#include <cstdint>

#include "envoy/common/pure.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/stream_info/stream_info.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Http {
namespace Http2 {

inline constexpr absl::string_view kKeepaliveObserverFilterStateKey =
    "envoy.http2.keepalive_observer";

class KeepaliveObserver : public StreamInfo::FilterState::Object {
public:
  ~KeepaliveObserver() override = default;

  virtual void onKeepalivePingSent(StreamInfo::StreamInfo& stream_info,
                                   uint64_t opaque_data) const PURE;
  virtual void onKeepalivePingAck(StreamInfo::StreamInfo& stream_info,
                                  uint64_t opaque_data) const PURE;
  virtual absl::optional<uint64_t> pendingKeepalivePingId() const PURE;
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
