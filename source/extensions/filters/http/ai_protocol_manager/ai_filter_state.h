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

// The request's wire API, set by a filter that knows the caller better than the route does. It is
// created from and serializes as an LLMProtocol value name, such as ANTHROPIC_MESSAGES.
class RequestLlmProtocol : public StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view FilterStateKey = "envoy.ai.llm_protocol.request";

  explicit RequestLlmProtocol(LLMProtocol protocol) : protocol_(protocol) {}

  // Unspecified when no object is set.
  static LLMProtocol fromFilterState(const StreamInfo::FilterState& filter_state);

  LLMProtocol protocol() const { return protocol_; }

  // StreamInfo::FilterState::Object
  std::optional<std::string> serializeAsString() const override;
  bool hasFieldSupport() const override { return true; }
  FieldType getField(absl::string_view field_name) const override;

private:
  const LLMProtocol protocol_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
