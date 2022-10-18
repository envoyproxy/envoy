#include "transport_socket_factory_context.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using ::testing::ReturnRef;

MockTransportSocketFactoryContext::MockTransportSocketFactoryContext()
    : secret_manager_(std::make_unique<Secret::SecretManagerImpl>(config_tracker_)),
      singleton_manager_(Thread::threadFactoryForTest()) {
  ON_CALL(*this, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
  ON_CALL(*this, sslContextManager()).WillByDefault(ReturnRef(context_manager_));
  ON_CALL(*this, scope()).WillByDefault(ReturnRef(store_));
  ON_CALL(*this, options()).WillByDefault(ReturnRef(options_));
  ON_CALL(*this, accessLogManager()).WillByDefault(ReturnRef(access_log_manager_));
  ON_CALL(*this, singletonManager()).WillByDefault(ReturnRef(singleton_manager_));
}

MockTransportSocketFactoryContext::~MockTransportSocketFactoryContext() = default;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
