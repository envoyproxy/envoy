#pragma once

#include <memory>
#include <utility>

#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// AiRequest represents the structured request payload presented to an AiFilter.
// It wraps a JsonWithExtBuf document as the request payload index.
// AiRequest is non-copyable and non-movable to ensure strict single-ownership semantics;
// ownership transfer across the filter pipeline is expressed via std::unique_ptr<AiRequest>.
class AiRequest {
public:
  explicit AiRequest(JsonWithExtBuf request_index) : request_index_(std::move(request_index)) {}
  ~AiRequest() = default;

  AiRequest(const AiRequest&) = delete;
  AiRequest& operator=(const AiRequest&) = delete;
  AiRequest(AiRequest&&) = delete;
  AiRequest& operator=(AiRequest&&) = delete;

  // Index access
  const JsonWithExtBuf& request_index() const { return request_index_; }
  JsonWithExtBuf& request_index() { return request_index_; }

  // TODO(penguingao): Implement field streaming (AiRequest::stream, FieldStreamingSpec,
  // and FieldStreamingSession).

private:
  JsonWithExtBuf request_index_;
};

using AiRequestPtr = std::unique_ptr<AiRequest>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
