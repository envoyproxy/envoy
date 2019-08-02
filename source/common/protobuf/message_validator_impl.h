#pragma once

#include "envoy/protobuf/message_validator.h"

namespace Envoy {
namespace ProtobufMessage {

class NullValidationVisitorImpl : public ValidationVisitor {
public:
  // Envoy::ProtobufMessage::ValidationVisitor
  void onUnknownField(absl::string_view) override {}
};

ValidationVisitor& getNullValidationVisitor();

class StrictValidationVisitorImpl : public ValidationVisitor {
public:
  // Envoy::ProtobufMessage::ValidationVisitor
  void onUnknownField(absl::string_view description) override;
};

ValidationVisitor& getStrictValidationVisitor();

class ValidationContextImpl : public ValidationContext {
public:
  ValidationContextImpl(ValidationVisitor& static_validation_visitor,
                        ValidationVisitor& dynamic_validation_visitor)
      : static_validation_visitor_(static_validation_visitor),
        dynamic_validation_visitor_(dynamic_validation_visitor) {}

  // Envoy::ProtobufMessage::ValidationContext
  ValidationVisitor& staticValidationVisitor() override { return static_validation_visitor_; }
  ValidationVisitor& dynamicValidationVisitor() override { return dynamic_validation_visitor_; }

private:
  ValidationVisitor& static_validation_visitor_;
  ValidationVisitor& dynamic_validation_visitor_;
};

} // namespace ProtobufMessage
} // namespace Envoy
