#pragma once

#include <optional>
#include <string>

#include "envoy/stream_info/filter_state.h"

#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Five transcoding instructions. A filter ahead of the transcoder filter can set
// any subset of them per request. The transcoder uses what is set and falls
// back to the route's declarations and its built-in defaults if unset.
namespace FilterStateKeys {
// A RequestLlmProtocol: the wire API of the request as received.
constexpr absl::string_view LlmProtocolRequest = "envoy.ai.llm_protocol.request";
// A ResponseLlmProtocol: the wire API of the target backend model.
constexpr absl::string_view LlmProtocolResponse = "envoy.ai.llm_protocol.response";
// The URI pattern the request path follows. See UriPattern.
constexpr absl::string_view UriPatternRequest = "envoy.ai.uri_pattern.request";
// The URI pattern the upstream expects. See UriPattern.
constexpr absl::string_view UriPatternResponse = "envoy.ai.uri_pattern.response";
// The model the request names. See Model.
constexpr absl::string_view ModelRequest = "envoy.ai.model.request";
// The model to send upstream, when it differs from the one the request names. It replaces the model
// in the request body and in the rewritten path.
constexpr absl::string_view ModelResolved = "envoy.ai.model.resolved";
} // namespace FilterStateKeys

// A wire API held in filter state. It is created from and serializes as an LLMProtocol value name,
// such as ANTHROPIC_MESSAGES.
class LlmProtocolFilterState : public StreamInfo::FilterState::Object {
public:
  explicit LlmProtocolFilterState(LLMProtocol protocol) : protocol_(protocol) {}

  LLMProtocol protocol() const { return protocol_; }

  // StreamInfo::FilterState::Object
  std::optional<std::string> serializeAsString() const override;
  bool hasFieldSupport() const override { return true; }
  FieldType getField(absl::string_view field_name) const override;

protected:
  // The protocol the object at `key` holds; Unspecified when none is set.
  static LLMProtocol read(const StreamInfo::FilterState& filter_state, absl::string_view key);

private:
  const LLMProtocol protocol_;
};

// The request's wire API, set by a filter that knows the caller better than the route does.
class RequestLlmProtocol : public LlmProtocolFilterState {
public:
  using LlmProtocolFilterState::LlmProtocolFilterState;

  // Unspecified when no object is set.
  static LLMProtocol fromFilterState(const StreamInfo::FilterState& filter_state) {
    return read(filter_state, FilterStateKeys::LlmProtocolRequest);
  }
};

// The upstream's wire API, set by a filter that chose the upstream, such as a model router.
class ResponseLlmProtocol : public LlmProtocolFilterState {
public:
  using LlmProtocolFilterState::LlmProtocolFilterState;

  // Unspecified when no object is set.
  static LLMProtocol fromFilterState(const StreamInfo::FilterState& filter_state) {
    return read(filter_state, FilterStateKeys::LlmProtocolResponse);
  }
};

// The string a Router::StringAccessor at `key` holds; nullopt when no object is set.
std::optional<absl::string_view> stringFromFilterState(const StreamInfo::FilterState& filter_state,
                                                       absl::string_view key);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
