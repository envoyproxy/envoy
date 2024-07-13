#pragma once

#include "envoy/server/instance.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/server/transport_socket_factory_context.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  Secret::SecretManager& secretManager() override { return *(secret_manager_); }

  MOCK_METHOD(OptRef<Admin>, admin, ());
  MOCK_METHOD(void, run, ());
  MOCK_METHOD(Api::Api&, api, ());
  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, ());
  MOCK_METHOD(const Upstream::ClusterManager&, clusterManager, (), (const));
  MOCK_METHOD(Ssl::ContextManager&, sslContextManager, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Network::DnsResolverSharedPtr, dnsResolver, ());
  MOCK_METHOD(void, drainListeners, (OptRef<const Network::ExtraShutdownListenerOptions> options));
  MOCK_METHOD(DrainManager&, drainManager, ());
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, ());
  MOCK_METHOD(void, failHealthcheck, (bool fail));
  MOCK_METHOD(bool, healthCheckFailed, ());
  MOCK_METHOD(HotRestart&, hotRestart, ());
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(ServerLifecycleNotifier&, lifecycleNotifier, ());
  MOCK_METHOD(ListenerManager&, listenerManager, ());
  MOCK_METHOD(Envoy::MutexTracer*, mutexTracer, ());
  MOCK_METHOD(const Options&, options, ());
  MOCK_METHOD(OverloadManager&, overloadManager, ());
  MOCK_METHOD(OverloadManager&, nullOverloadManager, ());
  MOCK_METHOD(bool, shouldBypassOverloadManager, (), (const));
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
  MOCK_METHOD(Router::Context&, routerContext, ());
  MOCK_METHOD(ProcessContextOptRef, processContext, ());
  MOCK_METHOD(ThreadLocal::Instance&, threadLocal, ());
  MOCK_METHOD(LocalInfo::LocalInfo&, localInfo, (), (const));
  MOCK_METHOD(Configuration::StatsConfig&, statsConfig, (), ());
  MOCK_METHOD(Regex::Engine&, regexEngine, ());
  MOCK_METHOD(void, flushStats, ());
  MOCK_METHOD(ProtobufMessage::ValidationContext&, messageValidationContext, ());
  MOCK_METHOD(Configuration::ServerFactoryContext&, serverFactoryContext, ());
  MOCK_METHOD(Configuration::TransportSocketFactoryContext&, transportSocketFactoryContext, ());
  MOCK_METHOD(bool, enableReusePortDefault, ());
  MOCK_METHOD(void, setSinkPredicates, (std::unique_ptr<Envoy::Stats::SinkPredicates> &&));

  void setDefaultTracingConfig(const envoy::config::trace::v3::Tracing& tracing_config) override {
    http_context_.setDefaultTracingConfig(tracing_config);
  }

  envoy::config::bootstrap::v3::Bootstrap& bootstrap() override { return bootstrap_; }

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
  testing::NiceMock<MockOverloadManager> null_overload_manager_;
  Singleton::ManagerPtr singleton_manager_;
  Grpc::ContextImpl grpc_context_;
  Http::ContextImpl http_context_;
  envoy::config::bootstrap::v3::Bootstrap bootstrap_;
  Router::ContextImpl router_context_;
  Quic::QuicStatNames quic_stat_names_;
  testing::NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  std::shared_ptr<testing::NiceMock<Configuration::MockStatsConfig>> stats_config_;
  std::shared_ptr<testing::NiceMock<Configuration::MockServerFactoryContext>>
      server_factory_context_;
  std::shared_ptr<testing::NiceMock<Configuration::MockTransportSocketFactoryContext>>
      transport_socket_factory_context_;
  Extensions::TransportSockets::Tls::ContextManagerImpl ssl_context_manager_;
  Regex::GoogleReEngine regex_engine_;
};

} // namespace Server
} // namespace Envoy
