#include "factory_context.h"

#include <string>

#include "source/common/singleton/manager_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using ::testing::ReturnRef;

MockListenerFactoryContext::MockListenerFactoryContext()
    : singleton_manager_(new Singleton::ManagerImpl(Thread::threadFactoryForTest())) {
  ON_CALL(*this, getServerFactoryContext()).WillByDefault(ReturnRef(server_factory_context_));
  ON_CALL(*this, drainDecision()).WillByDefault(ReturnRef(drain_manager_));
  ON_CALL(*this, lifecycleNotifier()).WillByDefault(ReturnRef(lifecycle_notifier_));
  ON_CALL(*this, localInfo()).WillByDefault(ReturnRef(local_info_));
  ON_CALL(*this, serverScope()).WillByDefault(ReturnRef(scope_));
  ON_CALL(*this, singletonManager()).WillByDefault(ReturnRef(*singleton_manager_));
  ON_CALL(*this, threadLocal()).WillByDefault(ReturnRef(thread_local_));
  ON_CALL(*this, listenerScope()).WillByDefault(ReturnRef(listener_scope_));
  ON_CALL(*this, timeSource()).WillByDefault(ReturnRef(time_system_));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
}

MockListenerFactoryContext::~MockListenerFactoryContext() = default;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
