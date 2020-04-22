#pragma once

#include "envoy/protobuf/message_validator.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace ProtobufMessage {

class MockValidationVisitor : public ValidationVisitor {
public:
  MockValidationVisitor();
  ~MockValidationVisitor() override;

  MOCK_METHOD(void, onUnknownField, (absl::string_view));
};

class MockValidationContext : public ValidationContext {
public:
  MockValidationContext();
  ~MockValidationContext() override;

  MOCK_METHOD(ValidationVisitor&, staticValidationVisitor, ());
  MOCK_METHOD(ValidationVisitor&, dynamicValidationVisitor, ());

  MockValidationVisitor static_validation_visitor_;
  MockValidationVisitor dynamic_validation_visitor_;
};

} // namespace ProtobufMessage
} // namespace Envoy
