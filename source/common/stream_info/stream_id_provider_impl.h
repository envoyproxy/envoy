#pragma once

#include "envoy/stream_info/stream_id_provider.h"

namespace Envoy {
namespace StreamInfo {

// TODO(wbpcode): This is super simple implementation. Could we
// make this a non-virtual simple struct in the future?
class StreamIdProviderImpl : public StreamIdProvider {
public:
  StreamIdProviderImpl(absl::string_view id) : id_(id) {}

  // StreamInfo::StreamIdProvider
  absl::string_view toStringView() const override { return id_; }
  // This is only supported for UUID format ids.
  absl::optional<uint64_t> toInteger() const override;

private:
  const std::string id_;
};

} // namespace StreamInfo
} // namespace Envoy
