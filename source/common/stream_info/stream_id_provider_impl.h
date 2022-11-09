#pragma once

#include "envoy/stream_info/stream_id_provider.h"

namespace Envoy {
namespace StreamInfo {

// Simple implementation of StreamIdProvider that returns the unique
// stream ID.
class StreamIdProviderImpl : public StreamIdProvider {
public:
  StreamIdProviderImpl(std::string&& id) : id_(std::move(id)) {}

  // StreamInfo::StreamIdProvider
  absl::optional<absl::string_view> toStringView() const override {
    return absl::make_optional<absl::string_view>(id_);
  }

  // This is only supported for UUID format ids.
  absl::optional<uint64_t> toInteger() const override;

private:
  const std::string id_;
};

} // namespace StreamInfo
} // namespace Envoy
