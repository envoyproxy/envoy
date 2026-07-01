#pragma once

#include <string>

#include "envoy/stream_info/stream_id_provider.h"

namespace Envoy {
namespace StreamInfo {

// Simple implementation of StreamIdProvider that returns the unique
// stream ID.
class StreamIdProviderImpl : public StreamIdProvider {
public:
  StreamIdProviderImpl(std::string&& id) : id_(std::move(id)) {}

  // StreamInfo::StreamIdProvider
  std::optional<absl::string_view> toStringView() const override {
    return std::make_optional<absl::string_view>(id_);
  }

  // This is only supported for UUID format ids.
  std::optional<uint64_t> toInteger() const override;

private:
  const std::string id_;
};

} // namespace StreamInfo
} // namespace Envoy
