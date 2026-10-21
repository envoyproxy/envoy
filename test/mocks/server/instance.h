#pragma once

#include "envoy/server/instance.h"

#include "test/mocks/config/xds_manager.h"
#include "test/mocks/http/http_server_properties_cache.h"
#include "test/mocks/server/server_factory_context.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  Secret::SecretManager& secretManager() override { return *(secret_manager_); }

  MOCK_METHOD(OptRef<Admin>, admin, (), (override));
  MOCK_METHOD(void, run, (), (override));
  MOCK_METHOD(Api::Api&, api, (), (override));
  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, (), (override));
  MOCK_METHOD(Config::XdsManager&, xdsManager, (), (override));
  MOCK_METHOD(const Upstream::ClusterManager&, clusterManager, (), (const, override));
  MOCK_METHOD(Http::HttpServerPropertiesCacheManager&, httpServerPropertiesCacheManager, (),
              (override));
  MOCK_METHOD(Ssl::ContextManager&, sslContextManager, (), (override));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (override));
  MOCK_METHOD(Network::DnsResolverSharedPtr, dnsResolver, (), (override));
  MOCK_METHOD(void, drainListeners, (OptRef<const Network::ExtraShutdownListenerOptions> options),
              (override));
  MOCK_METHOD(DrainManager&, drainManager, (), (override));
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, (), (override));
  MOCK_METHOD(void, failHealthcheck, (bool fail), (override));
  MOCK_METHOD(bool, healthCheckFailed, (), (override));
  MOCK_METHOD(HotRestart&, hotRestart, (), (override));
  MOCK_METHOD(Init::Manager&, initManager, (), (override));
  MOCK_METHOD(ServerLifecycleNotifier&, lifecycleNotifier, (), (override));
  MOCK_METHOD(ListenerManager&, listenerManager, (), (override));
  MOCK_METHOD(Envoy::MutexTracer*, mutexTracer, (), (override));
  MOCK_METHOD(const Options&, options, (), (override));
  MOCK_METHOD(OverloadManager&, overloadManager, (), (override));
  MOCK_METHOD(OverloadManager&, nullOverloadManager, (), (override));
  MOCK_METHOD(bool, shouldBypassOverloadManager, (), (const));
  MOCK_METHOD(Runtime::Loader&, runtime, (), (override));
  MOCK_METHOD(void, shutdown, (), (override));
  MOCK_METHOD(bool, isShutdown, (), (override));
  MOCK_METHOD(void, shutdownAdmin, (), (override));
  MOCK_METHOD(Singleton::Manager&, singletonManager, (), (override));
  MOCK_METHOD(time_t, startTimeCurrentEpoch, (), (override));
  MOCK_METHOD(time_t, startTimeFirstEpoch, (), (override));
  MOCK_METHOD(Stats::Store&, stats, (), (override));
  MOCK_METHOD(Grpc::Context&, grpcContext, (), (override));
  MOCK_METHOD(Http::Context&, httpContext, (), (override));
  MOCK_METHOD(Router::Context&, routerContext, (), (override));
  MOCK_METHOD(ProcessContextOptRef, processContext, (), (override));
  MOCK_METHOD(ThreadLocal::Instance&, threadLocal, (), (override));
  MOCK_METHOD(LocalInfo::LocalInfo&, localInfo, (), (const, override));
  MOCK_METHOD(Configuration::StatsConfig&, statsConfig, (), (override));
  MOCK_METHOD(Regex::Engine&, regexEngine, (), (override));
  MOCK_METHOD(void, flushStats, (), (override));
  MOCK_METHOD(ProtobufMessage::ValidationContext&, messageValidationContext, (), (override));
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, (), (override));
  MOCK_METHOD(Configuration::ServerFactoryContext&, serverFactoryContext, (), (override));
  MOCK_METHOD(Configuration::TransportSocketFactoryContext&, transportSocketFactoryContext, (),
              (override));
  MOCK_METHOD(bool, enableReusePortDefault, (), (override));
  MOCK_METHOD(void, setSinkPredicates, (std::unique_ptr<Envoy::Stats::SinkPredicates> &&),
              (override));

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
  testing::NiceMock<Config::MockXdsManager> xds_manager_;
  testing::NiceMock<Http::MockHttpServerPropertiesCacheManager>
      http_server_properties_cache_manager_;
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
