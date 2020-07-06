#include "tracer_factory_context.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using ::testing::ReturnRef;

MockTracerFactoryContext::MockTracerFactoryContext() {
  ON_CALL(*this, serverFactoryContext()).WillByDefault(ReturnRef(server_factory_context_));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
}

MockTracerFactoryContext::~MockTracerFactoryContext() = default;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
