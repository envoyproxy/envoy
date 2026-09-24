#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"

#include <memory>
#include <string>

#include "envoy/registry/registry.h"
#include "envoy/type/ai/v3/downstream_api.pb.validate.h"
#include "envoy/type/ai/v3/upstream_target.pb.validate.h"

#include "source/common/common/logger.h"
#include "source/common/http/header_utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ai_protocol_manager/endpoint_layout.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_conversion.h"

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

// A host with an optional numeric port; no userinfo.
bool isHostAndPort(absl::string_view authority) {
  if (!Http::HeaderUtility::authorityIsValid(authority) || absl::StrContains(authority, '@')) {
    return false;
  }
  absl::string_view host = authority;
  if (const size_t colon = authority.rfind(':');
      colon != absl::string_view::npos && authority.back() != ']') {
    host = authority.substr(0, colon);
    const absl::string_view port = authority.substr(colon + 1);
    uint32_t port_number = 0;
    if (port.empty() || port.size() > 5 || !absl::c_all_of(port, absl::ascii_isdigit) ||
        !absl::SimpleAtoi(port, &port_number) || port_number == 0 || port_number > 65535) {
      return false;
    }
  }
  return !host.empty();
}

template <class Proto> absl::Status validateProto(const Proto& proto) {
  if (std::string error; !Validate(proto, &error)) {
    return absl::InvalidArgumentError(error);
  }
  if (proto.has_endpoint()) {
    return validateEndpoint(proto.endpoint(), proto.llm_protocol());
  }
  return absl::OkStatus();
}

absl::Status validateUpstreamTarget(const envoy::type::ai::v3::UpstreamTarget& target) {
  if (absl::Status status = validateProto(target); !status.ok()) {
    return status;
  }
  if (!target.authority().empty() && !isHostAndPort(target.authority())) {
    return absl::InvalidArgumentError(
        absl::StrCat("authority '", target.authority(), "' is not a valid host[:port]"));
  }
  return absl::OkStatus();
}

absl::Status loadJson(absl::string_view json, Protobuf::Message& proto) {
  bool has_unknown_field = false;
  return MessageUtil::loadFromJsonNoThrow(json, proto, has_unknown_field);
}

void logRejected(absl::string_view key, const absl::Status& status) {
  ENVOY_LOG_MISC(debug, "invalid {} filter state: {}", key, status.message());
}

} // namespace

const DownstreamApiState*
DownstreamApiState::fromFilterState(const StreamInfo::FilterState& filter_state) {
  return filter_state.getDataReadOnly<DownstreamApiState>(kFilterStateKey);
}

LLMProtocol DownstreamApiState::protocol() const { return protocolFromProto(api_->llm_protocol()); }

ProtobufTypes::MessagePtr DownstreamApiState::serializeAsProto() const {
  return std::make_unique<envoy::type::ai::v3::DownstreamApi>(*api_);
}

std::optional<std::string> DownstreamApiState::serializeAsString() const {
  return MessageUtil::getJsonStringFromMessageOrError(*api_);
}

StreamInfo::FilterState::Object::FieldType
DownstreamApiState::getField(absl::string_view field_name) const {
  // The views are backed by string literals or by the held proto, so they outlive the call.
  if (field_name == "llm_protocol") {
    return llmProtocolName(protocol());
  }
  if (field_name == "preset") {
    return absl::string_view(api_->endpoint().preset());
  }
  return absl::monostate{};
}

const UpstreamTargetState*
UpstreamTargetState::fromFilterState(const StreamInfo::FilterState& filter_state) {
  return filter_state.getDataReadOnly<UpstreamTargetState>(kFilterStateKey);
}

LLMProtocol UpstreamTargetState::protocol() const {
  return protocolFromProto(target_->llm_protocol());
}

ProtobufTypes::MessagePtr UpstreamTargetState::serializeAsProto() const {
  return std::make_unique<envoy::type::ai::v3::UpstreamTarget>(*target_);
}

std::optional<std::string> UpstreamTargetState::serializeAsString() const {
  return MessageUtil::getJsonStringFromMessageOrError(*target_);
}

StreamInfo::FilterState::Object::FieldType
UpstreamTargetState::getField(absl::string_view field_name) const {
  if (field_name == "llm_protocol") {
    return llmProtocolName(protocol());
  }
  if (field_name == "authority") {
    return absl::string_view(target_->authority());
  }
  if (field_name == "model") {
    return absl::string_view(target_->model());
  }
  if (field_name == "preset") {
    return absl::string_view(target_->endpoint().preset());
  }
  if (field_name == "credential") {
    return absl::string_view(target_->credential());
  }
  return absl::monostate{};
}

namespace {

class DownstreamApiObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(DownstreamApiState::kFilterStateKey); }

  // A bare LLMProtocol value name is shorthand for a DownstreamApi naming only that protocol.
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    envoy::type::ai::v3::DownstreamApi api;
    envoy::type::ai::v3::LLMProtocol protocol;
    absl::Status status;
    if (envoy::type::ai::v3::LLMProtocol_Parse(std::string(data), &protocol)) {
      api.set_llm_protocol(protocol);
    } else {
      status = loadJson(data, api);
    }
    if (status.ok()) {
      status = validateProto(api);
    }
    if (!status.ok()) {
      logRejected(DownstreamApiState::kFilterStateKey, status);
      return nullptr;
    }
    return std::make_unique<DownstreamApiState>(
        std::make_shared<const envoy::type::ai::v3::DownstreamApi>(std::move(api)));
  }
};

class UpstreamTargetObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(UpstreamTargetState::kFilterStateKey); }

  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    envoy::type::ai::v3::UpstreamTarget target;
    absl::Status status = loadJson(data, target);
    if (status.ok()) {
      status = validateUpstreamTarget(target);
    }
    if (!status.ok()) {
      logRejected(UpstreamTargetState::kFilterStateKey, status);
      return nullptr;
    }
    return std::make_unique<UpstreamTargetState>(
        std::make_shared<const envoy::type::ai::v3::UpstreamTarget>(std::move(target)));
  }
};

REGISTER_FACTORY(DownstreamApiObjectFactory, StreamInfo::FilterState::ObjectFactory);
REGISTER_FACTORY(UpstreamTargetObjectFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
