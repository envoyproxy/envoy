#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// What an AI filter can ask the AI Protocol Manager to do with the matched route once the AI
// filters are done.
enum class AiRouteAction {
  // Pick the route's cluster again where its cluster specifier supports it, such as the matcher
  // plugin; the route is not matched again.
  RefreshCluster = 0x1,
};

// AiRequest represents the structured request payload presented to an AiFilter.
// It wraps a JsonWithExtBuf document as the request payload index.
// AiRequest is non-copyable and non-movable to ensure strict single-ownership semantics;
// ownership transfer across the filter pipeline is expressed via std::unique_ptr<AiRequest>.
class AiRequest {
public:
  explicit AiRequest(JsonWithExtBuf request_index, LLMProtocol protocol = LLMProtocol::Unspecified)
      : request_index_(std::move(request_index)), protocol_(protocol) {}
  ~AiRequest() = default;

  AiRequest(const AiRequest&) = delete;
  AiRequest& operator=(const AiRequest&) = delete;
  AiRequest(AiRequest&&) = delete;
  AiRequest& operator=(AiRequest&&) = delete;

  // The payload DOM -- the request's only mutable surface.
  const nlohmann::json& json() const { return request_index_.json(); }
  nlohmann::json& json() { return request_index_.json(); }

  // The whole index, external-buffer references included; only serialization needs it.
  const JsonWithExtBuf& request_index() const { return request_index_; }

  // Hands the index to the sink once the filters are done with the request.
  JsonWithExtBuf takeRequestIndex() { return std::move(request_index_); }

  // The payload's wire API, seeded with the declared one. A filter that identifies it sets it for
  // the filters after it.
  LLMProtocol protocol() const { return protocol_; }
  void setProtocol(LLMProtocol protocol) { protocol_ = protocol; }

  // Asks the AI Protocol Manager for `action`. Call it before propagating; it cannot be withdrawn.
  void requestRouteAction(AiRouteAction action) { route_actions_ |= static_cast<uint32_t>(action); }
  bool routeActionRequested(AiRouteAction action) const {
    return (route_actions_ & static_cast<uint32_t>(action)) != 0;
  }

  // TODO(penguingao): Implement field streaming (AiRequest::stream, FieldStreamingSpec,
  // and FieldStreamingSession).

private:
  JsonWithExtBuf request_index_;
  LLMProtocol protocol_;
  // A bitset of AiRouteActions.
  uint32_t route_actions_{0};
};

using AiRequestPtr = std::unique_ptr<AiRequest>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
