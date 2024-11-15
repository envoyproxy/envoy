#include "test/mocks/protobuf/mocks.h"

using testing::ReturnRef;

namespace Envoy {
namespace ProtobufMessage {

MockValidationVisitor::MockValidationVisitor() = default;

MockValidationVisitor::~MockValidationVisitor() = default;

MockValidationContext::MockValidationContext() {
  ON_CALL(*this, staticValidationVisitor()).WillByDefault(ReturnRef(static_validation_visitor_));
  ON_CALL(*this, dynamicValidationVisitor()).WillByDefault(ReturnRef(dynamic_validation_visitor_));
}

MockValidationContext::~MockValidationContext() = default;

} // namespace ProtobufMessage
} // namespace Envoy
