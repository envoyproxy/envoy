#include "instance.h"

#include "common/singleton/manager_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::Return;
using ::testing::ReturnRef;

MockInstance::MockInstance()
    : secret_manager_(std::make_unique<Secret::SecretManagerImpl>(admin_.getConfigTracker())),
      cluster_manager_(timeSource()), ssl_context_manager_(timeSource()),
      singleton_manager_(new Singleton::ManagerImpl(Thread::threadFactoryForTest())),
      grpc_context_(stats_store_.symbolTable()), http_context_(stats_store_.symbolTable()),
      server_factory_context_(
          std::make_shared<NiceMock<Configuration::MockServerFactoryContext>>()),
      transport_socket_factory_context_(
          std::make_shared<NiceMock<Configuration::MockTransportSocketFactoryContext>>()) {
  ON_CALL(*this, threadLocal()).WillByDefault(ReturnRef(thread_local_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_store_));
  ON_CALL(*this, grpcContext()).WillByDefault(ReturnRef(grpc_context_));
  ON_CALL(*this, httpContext()).WillByDefault(ReturnRef(http_context_));
  ON_CALL(*this, dnsResolver()).WillByDefault(Return(dns_resolver_));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
  ON_CALL(*this, admin()).WillByDefault(ReturnRef(admin_));
  ON_CALL(*this, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  ON_CALL(*this, sslContextManager()).WillByDefault(ReturnRef(ssl_context_manager_));
  ON_CALL(*this, accessLogManager()).WillByDefault(ReturnRef(access_log_manager_));
  ON_CALL(*this, runtime()).WillByDefault(ReturnRef(runtime_loader_));
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
  ON_CALL(*this, hotRestart()).WillByDefault(ReturnRef(hot_restart_));
  ON_CALL(*this, lifecycleNotifier()).WillByDefault(ReturnRef(lifecycle_notifier_));
  ON_CALL(*this, localInfo()).WillByDefault(ReturnRef(local_info_));
  ON_CALL(*this, options()).WillByDefault(ReturnRef(options_));
  ON_CALL(*this, drainManager()).WillByDefault(ReturnRef(drain_manager_));
  ON_CALL(*this, initManager()).WillByDefault(ReturnRef(init_manager_));
  ON_CALL(*this, listenerManager()).WillByDefault(ReturnRef(listener_manager_));
  ON_CALL(*this, mutexTracer()).WillByDefault(Return(nullptr));
  ON_CALL(*this, singletonManager()).WillByDefault(ReturnRef(*singleton_manager_));
  ON_CALL(*this, overloadManager()).WillByDefault(ReturnRef(overload_manager_));
  ON_CALL(*this, messageValidationContext()).WillByDefault(ReturnRef(validation_context_));
  ON_CALL(*this, serverFactoryContext()).WillByDefault(ReturnRef(*server_factory_context_));
  ON_CALL(*this, transportSocketFactoryContext())
      .WillByDefault(ReturnRef(*transport_socket_factory_context_));
}

MockInstance::~MockInstance() = default;

namespace Configuration {

MockServerFactoryContext::MockServerFactoryContext()
    : singleton_manager_(new Singleton::ManagerImpl(Thread::threadFactoryForTest())),
      grpc_context_(scope_.symbolTable()) {
  ON_CALL(*this, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
  ON_CALL(*this, drainDecision()).WillByDefault(ReturnRef(drain_manager_));
  ON_CALL(*this, localInfo()).WillByDefault(ReturnRef(local_info_));
  ON_CALL(*this, runtime()).WillByDefault(ReturnRef(runtime_loader_));
  ON_CALL(*this, scope()).WillByDefault(ReturnRef(scope_));
  ON_CALL(*this, singletonManager()).WillByDefault(ReturnRef(*singleton_manager_));
  ON_CALL(*this, threadLocal()).WillByDefault(ReturnRef(thread_local_));
  ON_CALL(*this, admin()).WillByDefault(ReturnRef(admin_));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
  ON_CALL(*this, timeSource()).WillByDefault(ReturnRef(time_system_));
  ON_CALL(*this, messageValidationContext()).WillByDefault(ReturnRef(validation_context_));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
  ON_CALL(*this, drainManager()).WillByDefault(ReturnRef(drain_manager_));
}
MockServerFactoryContext::~MockServerFactoryContext() = default;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
