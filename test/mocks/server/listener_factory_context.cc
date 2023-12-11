#include "listener_factory_context.h"

#include <string>

#include "source/common/singleton/manager_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using ::testing::ReturnRef;

MockListenerFactoryContext::MockListenerFactoryContext() {
  ON_CALL(*this, serverFactoryContext()).WillByDefault(ReturnRef(server_factory_context_));
  ON_CALL(*this, drainDecision()).WillByDefault(ReturnRef(drain_manager_));
  ON_CALL(*this, initManager()).WillByDefault(ReturnRef(init_manager_));
  ON_CALL(*this, scope()).WillByDefault(ReturnRef(scope_));
  ON_CALL(*this, listenerScope()).WillByDefault(ReturnRef(*listener_scope_.rootScope()));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
}

MockListenerFactoryContext::~MockListenerFactoryContext() = default;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
