#pragma once

#include <memory>
#include <optional>
#include <string>

#include "envoy/stream_info/filter_state.h"
#include "envoy/type/ai/v3/downstream_api.pb.h"
#include "envoy/type/ai/v3/upstream_target.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

using DownstreamApiConstSharedPtr = std::shared_ptr<const envoy::type::ai::v3::DownstreamApi>;
using UpstreamTargetConstSharedPtr = std::shared_ptr<const envoy::type::ai::v3::UpstreamTarget>;

// How the client speaks to the gateway, named by a filter that knows the caller rather than by the
// route that matched. The AI Protocol Manager takes its protocol ahead of the route's declaration,
// and pins the route's there otherwise.
//
// A factory registered under kFilterStateKey builds it from an envoy.type.ai.v3.LLMProtocol value
// name such as ANTHROPIC_MESSAGES, or from the JSON of an envoy.type.ai.v3.DownstreamApi, and
// yields nothing for an invalid one. It serializes as that JSON.
class DownstreamApiState : public StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view kFilterStateKey = "envoy.ai.downstream_api";

  explicit DownstreamApiState(DownstreamApiConstSharedPtr api) : api_(std::move(api)) {}

  // The object under kFilterStateKey, or nullptr when there is none or it is of another type.
  static const DownstreamApiState* fromFilterState(const StreamInfo::FilterState& filter_state);

  LLMProtocol protocol() const;
  const DownstreamApiConstSharedPtr& api() const { return api_; }

  // StreamInfo::FilterState::Object
  ProtobufTypes::MessagePtr serializeAsProto() const override;
  std::optional<std::string> serializeAsString() const override;
  bool hasFieldSupport() const override { return true; }
  FieldType getField(absl::string_view field_name) const override;

private:
  const DownstreamApiConstSharedPtr api_;
};

// The complete description of the upstream a request is sent to; it carries the protocol the
// upstream speaks. Only trusted, config-driven writers may set it, and never from request content.
//
// A factory registered under kFilterStateKey builds it from the JSON of an
// envoy.type.ai.v3.UpstreamTarget, and yields nothing for an invalid one.
class UpstreamTargetState : public StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view kFilterStateKey = "envoy.ai.upstream_target";

  explicit UpstreamTargetState(UpstreamTargetConstSharedPtr target) : target_(std::move(target)) {}

  static const UpstreamTargetState* fromFilterState(const StreamInfo::FilterState& filter_state);

  LLMProtocol protocol() const;
  const UpstreamTargetConstSharedPtr& target() const { return target_; }

  // StreamInfo::FilterState::Object
  ProtobufTypes::MessagePtr serializeAsProto() const override;
  std::optional<std::string> serializeAsString() const override;
  bool hasFieldSupport() const override { return true; }
  FieldType getField(absl::string_view field_name) const override;

private:
  const UpstreamTargetConstSharedPtr target_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
