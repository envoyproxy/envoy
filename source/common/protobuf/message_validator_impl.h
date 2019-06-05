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

} // namespace ProtobufMessage
} // namespace Envoy
