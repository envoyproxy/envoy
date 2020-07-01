#pragma once

#include "envoy/config/subscription.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

template <typename Current> class OpaqueResourceDecoderImpl : public Config::OpaqueResourceDecoder {
public:
  OpaqueResourceDecoderImpl(ProtobufMessage::ValidationVisitor& validation_visitor,
                            absl::string_view name_field)
      : validation_visitor_(validation_visitor), name_field_(name_field) {}

  // Config::OpaqueResourceDecoder
  ProtobufTypes::MessagePtr decodeResource(const ProtobufWkt::Any& resource) override {
    auto typed_message = std::make_unique<Current>();
    // If the Any is a synthetic empty message (e.g. because the resource field was not set in
    // Resource, this might be empty, so we shouldn't decode.
    if (!resource.type_url().empty()) {
      MessageUtil::anyConvertAndValidate<Current>(resource, *typed_message, validation_visitor_);
    }
    return typed_message;
  }

  std::string resourceName(const Protobuf::Message& resource) override {
    return MessageUtil::getStringField(resource, name_field_);
  }

private:
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  const std::string name_field_;
};

} // namespace Config
} // namespace Envoy
