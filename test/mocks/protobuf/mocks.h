#pragma once

#include "envoy/protobuf/message_validator.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace ProtobufMessage {

class MockValidationVisitor : public ValidationVisitor {
public:
  MockValidationVisitor();
  ~MockValidationVisitor();

  MOCK_METHOD1(onUnknownField, void(absl::string_view));
};

} // namespace ProtobufMessage
} // namespace Envoy
