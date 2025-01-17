#include "test/mocks/server/instance.h"

#include "source/common/singleton/manager_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::Return;
using ::testing::ReturnRef;

MockInstance::MockInstance()
    : secret_manager_(std::make_unique<Secret::SecretManagerImpl>(admin_.getConfigTracker())),
      cluster_manager_(timeSource()), singleton_manager_(new Singleton::ManagerImpl()),
      grpc_context_(stats_store_.symbolTable()), http_context_(stats_store_.symbolTable()),
      router_context_(stats_store_.symbolTable()), quic_stat_names_(stats_store_.symbolTable()),
      stats_config_(std::make_shared<NiceMock<Configuration::MockStatsConfig>>()),
      server_factory_context_(
          std::make_shared<NiceMock<Configuration::MockServerFactoryContext>>()),
      transport_socket_factory_context_(
          std::make_shared<NiceMock<Configuration::MockTransportSocketFactoryContext>>()),
      ssl_context_manager_(*server_factory_context_) {
  ON_CALL(*this, threadLocal()).WillByDefault(ReturnRef(thread_local_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_store_));
  ON_CALL(*this, grpcContext()).WillByDefault(ReturnRef(grpc_context_));
  ON_CALL(*this, httpContext()).WillByDefault(ReturnRef(http_context_));
  ON_CALL(*this, routerContext()).WillByDefault(ReturnRef(router_context_));
  ON_CALL(*this, dnsResolver()).WillByDefault(Return(dns_resolver_));
  ON_CALL(*this, api()).WillByDefault(ReturnRef(api_));
  ON_CALL(*this, admin()).WillByDefault(Return(OptRef<Server::Admin>{admin_}));
  ON_CALL(*this, clusterManager()).WillByDefault(ReturnRef(cluster_manager_));
  ON_CALL(*this, httpServerPropertiesCacheManager())
      .WillByDefault(ReturnRef(http_server_properties_cache_manager_));
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
  ON_CALL(*this, nullOverloadManager()).WillByDefault(ReturnRef(null_overload_manager_));
  ON_CALL(*this, messageValidationContext()).WillByDefault(ReturnRef(validation_context_));
  ON_CALL(*this, statsConfig()).WillByDefault(ReturnRef(*stats_config_));
  ON_CALL(*this, regexEngine()).WillByDefault(ReturnRef(regex_engine_));
  ON_CALL(*this, serverFactoryContext()).WillByDefault(ReturnRef(*server_factory_context_));
  ON_CALL(*this, transportSocketFactoryContext())
      .WillByDefault(ReturnRef(*transport_socket_factory_context_));
  ON_CALL(*this, enableReusePortDefault()).WillByDefault(Return(true));
}

MockInstance::~MockInstance() = default;

} // namespace Server
} // namespace Envoy
