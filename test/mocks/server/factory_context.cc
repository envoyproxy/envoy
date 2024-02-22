#include "factory_context.h"

#include <string>

#include "source/common/singleton/manager_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using ::testing::ReturnRef;

MockFactoryContext::MockFactoryContext() {
  ON_CALL(*this, serverFactoryContext()).WillByDefault(ReturnRef(server_factory_context_));
  ON_CALL(*this, initManager()).WillByDefault(ReturnRef(init_manager_));
  ON_CALL(*this, scope()).WillByDefault(ReturnRef(scope_));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));

  ON_CALL(*this, getTransportSocketFactoryContext())
      .WillByDefault(ReturnRef(transport_socket_factory_context_));
  ON_CALL(*this, drainDecision()).WillByDefault(ReturnRef(drain_manager_));
  ON_CALL(*this, listenerScope()).WillByDefault(ReturnRef(*listener_store_.rootScope()));
}

MockFactoryContext::~MockFactoryContext() = default;

MockUpstreamFactoryContext::MockUpstreamFactoryContext() {
  ON_CALL(*this, serverFactoryContext()).WillByDefault(ReturnRef(server_factory_context_));
  ON_CALL(*this, initManager()).WillByDefault(ReturnRef(init_manager_));
  ON_CALL(*this, scope()).WillByDefault(ReturnRef(scope_));
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
