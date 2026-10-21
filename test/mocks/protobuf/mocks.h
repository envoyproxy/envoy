#pragma once

#include "envoy/protobuf/message_validator.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace ProtobufMessage {

class MockValidationVisitor : public ValidationVisitor {
public:
  MockValidationVisitor();
  ~MockValidationVisitor() override;

  MOCK_METHOD(absl::Status, onUnknownField, (absl::string_view), (override));
  MOCK_METHOD(absl::Status, onDeprecatedField, (absl::string_view, bool), (override));
  MOCK_METHOD(void, onWorkInProgress, (absl::string_view), (override));
  MOCK_METHOD(OptRef<Runtime::Loader>, runtime, (), (override));

  bool skipValidation() override { return skip_validation_; }
  void setSkipValidation(bool s) { skip_validation_ = s; }

private:
  bool skip_validation_ = false;
};

class MockValidationContext : public ValidationContext {
public:
  MockValidationContext();
  ~MockValidationContext() override;

  MOCK_METHOD(ValidationVisitor&, staticValidationVisitor, (), (override));
  MOCK_METHOD(ValidationVisitor&, dynamicValidationVisitor, (), (override));

  testing::NiceMock<MockValidationVisitor> static_validation_visitor_;
  testing::NiceMock<MockValidationVisitor> dynamic_validation_visitor_;
};

} // namespace ProtobufMessage
} // namespace Envoy
