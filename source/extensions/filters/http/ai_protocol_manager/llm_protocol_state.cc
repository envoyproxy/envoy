#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_state.h"

#include <memory>
#include <string>

#include "envoy/registry/registry.h"
#include "envoy/type/ai/v3/upstream_target.pb.validate.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_conversion.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

LLMProtocol RequestLlmProtocol::fromFilterState(const StreamInfo::FilterState& filter_state) {
  const auto* declared = filter_state.getDataReadOnly<RequestLlmProtocol>(kFilterStateKey);
  return declared != nullptr ? declared->protocol() : LLMProtocol::Unspecified;
}

std::optional<std::string> RequestLlmProtocol::serializeAsString() const {
  return std::string(llmProtocolName(protocol_));
}

StreamInfo::FilterState::Object::FieldType
RequestLlmProtocol::getField(absl::string_view field_name) const {
  if (field_name == "llm_protocol") {
    // Backed by a string literal, so the view outlives the call.
    return llmProtocolName(protocol_);
  }
  return absl::monostate{};
}

LLMProtocol UpstreamTargetState::protocol() const {
  return protocolFromProto(target_.llm_protocol());
}

std::optional<std::string> UpstreamTargetState::serializeAsString() const {
  return MessageUtil::getJsonStringFromMessageOrError(target_);
}

StreamInfo::FilterState::Object::FieldType
UpstreamTargetState::getField(absl::string_view field_name) const {
  if (field_name == "llm_protocol") {
    return llmProtocolName(protocol());
  }
  return absl::monostate{};
}

namespace {

class RequestLlmProtocolObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(RequestLlmProtocol::kFilterStateKey); }

  // A name the enum does not define yields no object, so a typo cannot read as "no protocol".
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    envoy::type::ai::v3::LLMProtocol protocol;
    if (!envoy::type::ai::v3::LLMProtocol_Parse(std::string(data), &protocol)) {
      return nullptr;
    }
    return std::make_unique<RequestLlmProtocol>(protocolFromProto(protocol));
  }
};

class UpstreamTargetObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(UpstreamTargetState::kFilterStateKey); }

  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    envoy::type::ai::v3::UpstreamTarget proto;
    bool has_unknown_field = false;
    if (absl::Status status = MessageUtil::loadFromJsonNoThrow(data, proto, has_unknown_field);
        !status.ok()) {
      ENVOY_LOG_MISC(debug, "invalid {} filter state: {}", UpstreamTargetState::kFilterStateKey,
                     status.message());
      return nullptr;
    }
    std::string error;
    if (!Validate(proto, &error)) {
      ENVOY_LOG_MISC(debug, "invalid {} filter state: {}", UpstreamTargetState::kFilterStateKey,
                     error);
      return nullptr;
    }
    if (proto.llm_protocol() == envoy::type::ai::v3::LLM_PROTOCOL_UNSPECIFIED) {
      ENVOY_LOG_MISC(debug, "invalid {} filter state: llm_protocol is required",
                     UpstreamTargetState::kFilterStateKey);
      return nullptr;
    }
    return std::make_unique<UpstreamTargetState>(std::move(proto));
  }
};

REGISTER_FACTORY(RequestLlmProtocolObjectFactory, StreamInfo::FilterState::ObjectFactory);
REGISTER_FACTORY(UpstreamTargetObjectFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
