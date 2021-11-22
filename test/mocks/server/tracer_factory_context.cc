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
  ON_CALL(*this, transportSocketFactoryContext())
      .WillByDefault(ReturnRef(transport_socket_factory_context_));
}

MockTracerFactoryContext::~MockTracerFactoryContext() = default;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
