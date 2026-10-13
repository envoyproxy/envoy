#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"

#include <memory>
#include <string>

#include "envoy/registry/registry.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_conversion.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

LLMProtocol RequestLlmProtocol::fromFilterState(const StreamInfo::FilterState& filter_state) {
  const auto* object =
      filter_state.getDataReadOnly<RequestLlmProtocol>(FilterStateKeys::LlmProtocolRequest);
  return object != nullptr ? object->protocol() : LLMProtocol::Unspecified;
}

std::optional<std::string> RequestLlmProtocol::serializeAsString() const {
  return std::string(llmProtocolName(protocol_));
}

StreamInfo::FilterState::Object::FieldType
RequestLlmProtocol::getField(absl::string_view field_name) const {
  if (field_name == "llm_protocol") {
    return llmProtocolName(protocol_);
  }
  return absl::monostate{};
}

namespace {

class RequestLlmProtocolObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(FilterStateKeys::LlmProtocolRequest); }

  // An unknown name yields no object, so a typo cannot read as LLM_PROTOCOL_UNSPECIFIED.
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    envoy::type::ai::v3::LLMProtocol protocol;
    if (!envoy::type::ai::v3::LLMProtocol_Parse(std::string(data), &protocol)) {
      return nullptr;
    }
    return std::make_unique<RequestLlmProtocol>(protocolFromProto(protocol));
  }
};

REGISTER_FACTORY(RequestLlmProtocolObjectFactory, StreamInfo::FilterState::ObjectFactory);

class RequestModelObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(FilterStateKeys::ModelRequest); }

  // An empty name yields no object: absence is how an unknown model reads.
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    if (data.empty()) {
      return nullptr;
    }
    return std::make_unique<Router::StringAccessorImpl>(data);
  }
};

REGISTER_FACTORY(RequestModelObjectFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
