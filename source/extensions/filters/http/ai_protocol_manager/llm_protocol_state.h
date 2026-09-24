#pragma once

#include <memory>
#include <optional>
#include <string>

#include "envoy/stream_info/filter_state.h"
#include "envoy/type/ai/v3/upstream_target.pb.h"

#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// The LLM wire protocol a request payload follows, named by a filter that knows the caller rather
// than by the route that matched. The AI Protocol Manager takes it ahead of the route's own
// declaration, and pins the route's there otherwise.
//
// A factory registered under kFilterStateKey builds it from an envoy.type.ai.v3.LLMProtocol enum
// value name, so set_filter_state, Lua and ext_proc can set it. It serializes as that same name.
class RequestLlmProtocol : public StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view kFilterStateKey = "envoy.ai.llm_protocol.request";

  explicit RequestLlmProtocol(LLMProtocol protocol) : protocol_(protocol) {}

  LLMProtocol protocol() const { return protocol_; }

  // The protocol named in filter state, or Unspecified when nothing named one.
  static LLMProtocol fromFilterState(const StreamInfo::FilterState& filter_state);

  // StreamInfo::FilterState::Object
  std::optional<std::string> serializeAsString() const override;
  bool hasFieldSupport() const override { return true; }
  FieldType getField(absl::string_view field_name) const override;

private:
  const LLMProtocol protocol_;
};

// The complete description of the upstream a request is sent to; it carries the protocol the
// upstream speaks. Only trusted, config-driven writers may set it, and never from request content.
//
// A factory registered under kFilterStateKey builds it from the JSON of an
// envoy.type.ai.v3.UpstreamTarget, and yields nothing for an invalid one.
class UpstreamTargetState : public StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view kFilterStateKey = "envoy.ai.upstream_target";

  explicit UpstreamTargetState(envoy::type::ai::v3::UpstreamTarget target)
      : target_(std::move(target)) {}

  LLMProtocol protocol() const;

  // StreamInfo::FilterState::Object
  std::optional<std::string> serializeAsString() const override;
  bool hasFieldSupport() const override { return true; }
  FieldType getField(absl::string_view field_name) const override;

private:
  const envoy::type::ai::v3::UpstreamTarget target_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
