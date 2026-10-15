#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"

#include <memory>
#include <string>
#include <utility>

#include "envoy/registry/registry.h"
#include "envoy/router/string_accessor.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_conversion.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

LLMProtocol LlmProtocolFilterState::read(const StreamInfo::FilterState& filter_state,
                                         absl::string_view key) {
  const auto* object = filter_state.getDataReadOnly<LlmProtocolFilterState>(key);
  return object != nullptr ? object->protocol() : LLMProtocol::Unspecified;
}

std::optional<std::string> LlmProtocolFilterState::serializeAsString() const {
  return std::string(llmProtocolName(protocol_));
}

StreamInfo::FilterState::Object::FieldType
LlmProtocolFilterState::getField(absl::string_view field_name) const {
  if (field_name == "llm_protocol") {
    return llmProtocolName(protocol_);
  }
  return absl::monostate{};
}

LlmBindingObject::LlmBindingObject(LlmBindingConstSharedPtr binding)
    : source_(binding != nullptr && binding->pattern.has_value() ? binding->pattern->source() : ""),
      binding_(std::move(binding)) {}

LlmBindingObject::LlmBindingObject(std::string source,
                                   absl::StatusOr<LlmBindingConstSharedPtr> binding)
    : source_(std::move(source)), status_(binding.status()) {
  if (binding.ok()) {
    binding_ = *std::move(binding);
  }
}

std::unique_ptr<LlmBindingObject> LlmBindingObject::fromString(absl::string_view value) {
  return std::unique_ptr<LlmBindingObject>(
      new LlmBindingObject(std::string(value), LlmBinding::parse(value)));
}

std::shared_ptr<const LlmBindingObject>
LlmBindingObject::fromFilterState(StreamInfo::FilterState& filter_state, absl::string_view key) {
  std::shared_ptr<StreamInfo::FilterState::Object> object =
      filter_state.getDataSharedMutableGeneric(key);
  if (object == nullptr) {
    return nullptr;
  }
  if (auto binding = std::dynamic_pointer_cast<const LlmBindingObject>(object);
      binding != nullptr) {
    return binding;
  }
  // Golang's SetFilterState and the dynamic modules' bytes setter store plain strings.
  if (const auto* text = dynamic_cast<const Router::StringAccessor*>(object.get());
      text != nullptr) {
    return fromString(text->asString());
  }
  return std::shared_ptr<const LlmBindingObject>(
      new LlmBindingObject("", absl::InvalidArgumentError("the object is not a binding")));
}

StreamInfo::FilterState::Object::FieldType
LlmBindingObject::getField(absl::string_view field_name) const {
  if (!read_.has_value()) {
    return absl::monostate{};
  }
  if (field_name == "model") {
    if (read_->model.empty()) {
      return absl::monostate{};
    }
    return absl::string_view(read_->model);
  }
  const auto variable = read_->path_variables.find(field_name);
  if (variable == read_->path_variables.end()) {
    return absl::monostate{};
  }
  return absl::string_view(variable->second);
}

std::optional<absl::string_view> stringFromFilterState(const StreamInfo::FilterState& filter_state,
                                                       absl::string_view key) {
  const auto* object = filter_state.getDataReadOnly<Router::StringAccessor>(key);
  if (object == nullptr) {
    return std::nullopt;
  }
  return object->asString();
}

namespace {

// Builds a protocol object of type T from an LLMProtocol value name. An unknown name yields no
// object, so a typo cannot read as LLM_PROTOCOL_UNSPECIFIED.
template <class T>
std::unique_ptr<StreamInfo::FilterState::Object> protocolFromBytes(absl::string_view data) {
  envoy::type::ai::v3::LLMProtocol protocol;
  if (!envoy::type::ai::v3::LLMProtocol_Parse(std::string(data), &protocol)) {
    return nullptr;
  }
  return std::make_unique<T>(protocolFromProto(protocol));
}

// Builds a string object. An empty string yields no object: absence is how "not set" reads.
std::unique_ptr<StreamInfo::FilterState::Object> stringFromBytes(absl::string_view data) {
  if (data.empty()) {
    return nullptr;
  }
  return std::make_unique<Router::StringAccessorImpl>(data);
}

class RequestLlmProtocolObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(FilterStateKeys::LlmProtocolRequest); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return protocolFromBytes<RequestLlmProtocol>(data);
  }
};

REGISTER_FACTORY(RequestLlmProtocolObjectFactory, StreamInfo::FilterState::ObjectFactory);

class ResponseLlmProtocolObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(FilterStateKeys::LlmProtocolResponse); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return protocolFromBytes<ResponseLlmProtocol>(data);
  }
};

REGISTER_FACTORY(ResponseLlmProtocolObjectFactory, StreamInfo::FilterState::ObjectFactory);

// Builds a binding object. Only an empty string yields none: a value that does not parse still
// sets the key, so the request fails where the binding is read rather than going on without it.
std::unique_ptr<StreamInfo::FilterState::Object> bindingFromBytes(absl::string_view data) {
  if (data.empty()) {
    return nullptr;
  }
  return LlmBindingObject::fromString(data);
}

class RequestBindingObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(FilterStateKeys::BindingRequest); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return bindingFromBytes(data);
  }
};

REGISTER_FACTORY(RequestBindingObjectFactory, StreamInfo::FilterState::ObjectFactory);

class ResponseBindingObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(FilterStateKeys::BindingResponse); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return bindingFromBytes(data);
  }
};

REGISTER_FACTORY(ResponseBindingObjectFactory, StreamInfo::FilterState::ObjectFactory);

class RequestModelObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(FilterStateKeys::ModelRequest); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return stringFromBytes(data);
  }
};

REGISTER_FACTORY(RequestModelObjectFactory, StreamInfo::FilterState::ObjectFactory);

class ResolvedModelObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(FilterStateKeys::ModelResolved); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return stringFromBytes(data);
  }
};

REGISTER_FACTORY(ResolvedModelObjectFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
