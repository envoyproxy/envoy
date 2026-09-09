#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Filter state object holding the serialized JsonWithExtBuf index.
class APMRequestPayloadIndex : public StreamInfo::FilterState::Object {
public:
  explicit APMRequestPayloadIndex(JsonWithExtBuf index) : index_(std::move(index)) {}

  const JsonWithExtBuf& index() const { return index_; }

  static constexpr absl::string_view kFilterStateKey =
      "envoy.filters.http.ai_protocol_manager.request_json";

private:
  JsonWithExtBuf index_;
};

class Serializer {
public:
  struct SerializedOffsets {
    JsonWithExtBuf doc;
    uint64_t total_size{0};
  };

  // Dry-run calculation: computes new byte offsets for all ExternalRef nodes in doc as they
  // will appear in the serialized output stream, returning a new JsonWithExtBuf instance and
  // the total serialized byte length.
  static Coroutine::Task<absl::StatusOr<SerializedOffsets>>
  calculateSerializedOffsets(const JsonWithExtBuf& doc);

  // Streaming serialization: replays JSON tokens and offloaded buffer slices directly through
  // the BufferManager into the filter chain with watermark flow control, and returns a new
  // JsonWithExtBuf with updated byte offsets reflecting the emitted serialization.
  static Coroutine::Task<absl::StatusOr<JsonWithExtBuf>> serialize(const JsonWithExtBuf& doc,
                                                                   BufferManager* buffer_manager);
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
