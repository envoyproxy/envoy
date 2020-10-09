#pragma once

#include "envoy/server/instance.h"

#include "common/grpc/context_impl.h"
#include "common/http/context_impl.h"
#include "common/stats/symbol_table_impl.h"

#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "admin.h"
#include "drain_manager.h"
#include "gmock/gmock.h"
#include "hot_restart.h"
#include "listener_manager.h"
#include "options.h"
#include "overload_manager.h"
#include "server_lifecycle_notifier.h"
#include "transport_socket_factory_context.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockServerFactoryContext;
} // namespace Configuration

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  Secret::SecretManager& secretManager() override { return *(secret_manager_.get()); }

  MOCK_METHOD(Admin&, admin, ());
  MOCK_METHOD(Api::Api&, api, ());
  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, ());
  MOCK_METHOD(Ssl::ContextManager&, sslContextManager, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Network::DnsResolverSharedPtr, dnsResolver, ());
  MOCK_METHOD(void, drainListeners, ());
  MOCK_METHOD(DrainManager&, drainManager, ());
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, ());
  MOCK_METHOD(void, failHealthcheck, (bool fail));
  MOCK_METHOD(void, exportStatsToChild, (envoy::HotRestartMessage::Reply::Stats*));
  MOCK_METHOD(bool, healthCheckFailed, ());
  MOCK_METHOD(HotRestart&, hotRestart, ());
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(ServerLifecycleNotifier&, lifecycleNotifier, ());
  MOCK_METHOD(ListenerManager&, listenerManager, ());
  MOCK_METHOD(Envoy::MutexTracer*, mutexTracer, ());
  MOCK_METHOD(const Options&, options, ());
  MOCK_METHOD(OverloadManager&, overloadManager, ());
  MOCK_METHOD(Runtime::Loader&, runtime, ());
  MOCK_METHOD(void, shutdown, ());
  MOCK_METHOD(bool, isShutdown, ());
  MOCK_METHOD(void, shutdownAdmin, ());
  MOCK_METHOD(Singleton::Manager&, singletonManager, ());
  MOCK_METHOD(time_t, startTimeCurrentEpoch, ());
  MOCK_METHOD(time_t, startTimeFirstEpoch, ());
  MOCK_METHOD(Stats::Store&, stats, ());
  MOCK_METHOD(Grpc::Context&, grpcContext, ());
  MOCK_METHOD(Http::Context&, httpContext, ());
  MOCK_METHOD(ProcessContextOptRef, processContext, ());
  MOCK_METHOD(ThreadLocal::Instance&, threadLocal, ());
  MOCK_METHOD(const LocalInfo::LocalInfo&, localInfo, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, statsFlushInterval, (), (const));
  MOCK_METHOD(void, flushStats, ());
  MOCK_METHOD(ProtobufMessage::ValidationContext&, messageValidationContext, ());
  MOCK_METHOD(Configuration::ServerFactoryContext&, serverFactoryContext, ());
  MOCK_METHOD(Configuration::TransportSocketFactoryContext&, transportSocketFactoryContext, ());

  void setDefaultTracingConfig(const envoy::config::trace::v3::Tracing& tracing_config) override {
    http_context_.setDefaultTracingConfig(tracing_config);
  }

  TimeSource& timeSource() override { return time_system_; }

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  std::shared_ptr<testing::NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new testing::NiceMock<Network::MockDnsResolver>()};
  testing::NiceMock<Api::MockApi> api_;
  testing::NiceMock<MockAdmin> admin_;
  Event::GlobalTimeSystem time_system_;
  std::unique_ptr<Secret::SecretManager> secret_manager_;
  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Thread::MutexBasicLockable access_log_lock_;
  testing::NiceMock<Runtime::MockLoader> runtime_loader_;
  Extensions::TransportSockets::Tls::ContextManagerImpl ssl_context_manager_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<MockDrainManager> drain_manager_;
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  testing::NiceMock<MockHotRestart> hot_restart_;
  testing::NiceMock<MockOptions> options_;
  testing::NiceMock<MockServerLifecycleNotifier> lifecycle_notifier_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<MockListenerManager> listener_manager_;
  testing::NiceMock<MockOverloadManager> overload_manager_;
  Singleton::ManagerPtr singleton_manager_;
  Grpc::ContextImpl grpc_context_;
  Http::ContextImpl http_context_;
  testing::NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  std::shared_ptr<testing::NiceMock<Configuration::MockServerFactoryContext>>
      server_factory_context_;
  std::shared_ptr<testing::NiceMock<Configuration::MockTransportSocketFactoryContext>>
      transport_socket_factory_context_;
};

namespace Configuration {
class MockServerFactoryContext : public virtual ServerFactoryContext {
public:
  MockServerFactoryContext();
  ~MockServerFactoryContext() override;

  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(const Network::DrainDecision&, drainDecision, ());
  MOCK_METHOD(const LocalInfo::LocalInfo&, localInfo, (), (const));
  MOCK_METHOD(Envoy::Runtime::Loader&, runtime, ());
  MOCK_METHOD(Stats::Scope&, scope, ());
  MOCK_METHOD(Singleton::Manager&, singletonManager, ());
  MOCK_METHOD(ThreadLocal::Instance&, threadLocal, ());
  MOCK_METHOD(Server::Admin&, admin, ());
  MOCK_METHOD(TimeSource&, timeSource, ());
  Event::TestTimeSystem& timeSystem() { return time_system_; }
  MOCK_METHOD(ProtobufMessage::ValidationContext&, messageValidationContext, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());
  MOCK_METHOD(Api::Api&, api, ());
  Grpc::Context& grpcContext() override { return grpc_context_; }
  MOCK_METHOD(Server::DrainManager&, drainManager, ());
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(ServerLifecycleNotifier&, lifecycleNotifier, ());
  MOCK_METHOD(std::chrono::milliseconds, statsFlushInterval, (), (const));

  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<MockDrainManager> drain_manager_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Envoy::Runtime::MockLoader> runtime_loader_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> scope_;
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  testing::NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  Singleton::ManagerPtr singleton_manager_;
  testing::NiceMock<MockAdmin> admin_;
  Event::GlobalTimeSystem time_system_;
  testing::NiceMock<Api::MockApi> api_;
  Grpc::ContextImpl grpc_context_;
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy
