#pragma once

#include "envoy/server/factory_context.h"

#include "source/common/grpc/context_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/quic/quic_stat_names.h"
#include "source/common/router/context_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/tls/context_manager_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/config/xds_manager.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/http_server_properties_cache.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/drain_manager.h"
#include "test/mocks/server/hot_restart.h"
#include "test/mocks/server/listener_manager.h"
#include "test/mocks/server/options.h"
#include "test/mocks/server/overload_manager.h"
#include "test/mocks/server/server_lifecycle_notifier.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class MockStatsConfig : public virtual StatsConfig {
public:
  MockStatsConfig();
  ~MockStatsConfig() override;

  MOCK_METHOD(const std::list<Stats::SinkPtr>&, sinks, (), (const, override));
  MOCK_METHOD(std::chrono::milliseconds, flushInterval, (), (const, override));
  MOCK_METHOD(bool, flushOnAdmin, (), (const, override));
  MOCK_METHOD(const Stats::SinkPredicates*, sinkPredicates, (), (const));
  MOCK_METHOD(bool, enableDeferredCreationStats, (), (const, override));
  MOCK_METHOD(uint32_t, evictOnFlush, (), (const, override));
};

class MockServerFactoryContext : public virtual ServerFactoryContext {
public:
  MockServerFactoryContext();
  ~MockServerFactoryContext() override;

  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, (), (override));
  MOCK_METHOD(Config::XdsManager&, xdsManager, (), (override));
  MOCK_METHOD(Http::HttpServerPropertiesCacheManager&, httpServerPropertiesCacheManager, (),
              (override));
  MOCK_METHOD(Event::Dispatcher&, mainThreadDispatcher, (), (override));
  MOCK_METHOD(const Server::Options&, options, (), (override));
  MOCK_METHOD(const Network::DrainDecision&, drainDecision, ());
  MOCK_METHOD(const LocalInfo::LocalInfo&, localInfo, (), (const, override));
  MOCK_METHOD(Envoy::Runtime::Loader&, runtime, (), (override));
  MOCK_METHOD(Stats::Scope&, scope, (), (override));
  MOCK_METHOD(Stats::Scope&, serverScope, (), (override));
  MOCK_METHOD(Singleton::Manager&, singletonManager, (), (override));
  MOCK_METHOD(ThreadLocal::Instance&, threadLocal, (), (override));
  MOCK_METHOD(OptRef<Server::Admin>, admin, (), (override));
  MOCK_METHOD(TimeSource&, timeSource, (), (override));
  MOCK_METHOD(Event::TestTimeSystem&, timeSystem, ());
  MOCK_METHOD(ProtobufMessage::ValidationContext&, messageValidationContext, (), (override));
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, (), (override));
  MOCK_METHOD(Api::Api&, api, (), (override));
  MOCK_METHOD(Secret::SecretManager&, secretManager, (), (override));
  MOCK_METHOD(Ssl::ContextManager&, sslContextManager, (), (override));
  Http::Context& httpContext() override { return http_context_; }
  Grpc::Context& grpcContext() override { return grpc_context_; }
  Router::Context& routerContext() override { return router_context_; }
  MOCK_METHOD(ProcessContextOptRef, processContext, (), (override));
  envoy::config::bootstrap::v3::Bootstrap& bootstrap() override { return bootstrap_; }
  MOCK_METHOD(Server::DrainManager&, drainManager, (), (override));
  MOCK_METHOD(Init::Manager&, initManager, (), (override));
  MOCK_METHOD(ServerLifecycleNotifier&, lifecycleNotifier, (), (override));
  Regex::Engine& regexEngine() override { return regex_engine_; }
  MOCK_METHOD(StatsConfig&, statsConfig, (), (override));
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, (), (override));
  MOCK_METHOD(OverloadManager&, overloadManager, (), (override));
  MOCK_METHOD(OverloadManager&, nullOverloadManager, (), (override));
  MOCK_METHOD(bool, shouldBypassOverloadManager, (), (const));
  MOCK_METHOD(bool, healthCheckFailed, (), (const, override));

  // Useful for when a test needs to change a static secret, which normally cannot be changed or
  // re-added after it is set. This creates a new secret manager and deletes the old one.
  void resetSecretManager();

  testing::NiceMock<Config::MockXdsManager> xds_manager_;
  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<MockDrainManager> drain_manager_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Envoy::Runtime::MockLoader> runtime_loader_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> store_;
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  testing::NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  testing::NiceMock<MockStatsConfig> stats_config_;
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<MockServerLifecycleNotifier> lifecycle_notifier_;
  testing::NiceMock<Http::MockHttpServerPropertiesCacheManager>
      http_server_properties_cache_manager_;

  Singleton::ManagerPtr singleton_manager_;
  testing::NiceMock<MockAdmin> admin_;
  Event::GlobalTimeSystem time_system_;
  testing::NiceMock<Api::MockApi> api_;
  testing::NiceMock<MockOverloadManager> overload_manager_;
  testing::NiceMock<MockOverloadManager> null_overload_manager_;
  Http::ContextImpl http_context_;
  Grpc::ContextImpl grpc_context_;
  Router::ContextImpl router_context_;
  envoy::config::bootstrap::v3::Bootstrap bootstrap_;
  testing::NiceMock<MockOptions> options_;
  Regex::GoogleReEngine regex_engine_;

  // Secret manager.
  testing::NiceMock<MockConfigTracker> config_tracker_;
  std::unique_ptr<Secret::SecretManager> secret_manager_;
  // SSL context manager.
  testing::NiceMock<Ssl::MockContextManager> ssl_context_manager_;
};

class MockGenericFactoryContext : public GenericFactoryContext {
public:
  MockGenericFactoryContext();
  ~MockGenericFactoryContext() override;

  MOCK_METHOD(ServerFactoryContext&, serverFactoryContext, (), (override));
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, (), (override));
  MOCK_METHOD(Stats::Scope&, scope, (), (override));
  MOCK_METHOD(Stats::Scope&, statsScope, (), (override));
  MOCK_METHOD(Stats::Scope&, serverScope, ());
  MOCK_METHOD(Init::Manager&, initManager, (), (override));

  NiceMock<MockServerFactoryContext> server_context_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> store_;
  testing::NiceMock<Init::MockManager> init_manager_;
};

using MockTransportSocketFactoryContext = MockGenericFactoryContext;

// Stateless mock ServerFactoryContext for cases where it needs to be used concurrently in different
// threads. Global state in the MockServerFactoryContext causes thread safety issues in this case.
class StatelessMockServerFactoryContext : public virtual ServerFactoryContext {
public:
  StatelessMockServerFactoryContext() = default;
  ~StatelessMockServerFactoryContext() override = default;

  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, (), (override));
  MOCK_METHOD(Config::XdsManager&, xdsManager, (), (override));
  MOCK_METHOD(Http::HttpServerPropertiesCacheManager&, httpServerPropertiesCacheManager, (),
              (override));
  MOCK_METHOD(Event::Dispatcher&, mainThreadDispatcher, (), (override));
  MOCK_METHOD(const Server::Options&, options, (), (override));
  MOCK_METHOD(const Network::DrainDecision&, drainDecision, ());
  MOCK_METHOD(const LocalInfo::LocalInfo&, localInfo, (), (const, override));
  MOCK_METHOD(Envoy::Runtime::Loader&, runtime, (), (override));
  MOCK_METHOD(Stats::Scope&, scope, (), (override));
  MOCK_METHOD(Stats::Scope&, serverScope, (), (override));
  MOCK_METHOD(Singleton::Manager&, singletonManager, (), (override));
  MOCK_METHOD(ThreadLocal::Instance&, threadLocal, (), (override));
  MOCK_METHOD(OptRef<Server::Admin>, admin, (), (override));
  MOCK_METHOD(TimeSource&, timeSource, (), (override));
  MOCK_METHOD(Event::TestTimeSystem&, timeSystem, ());
  MOCK_METHOD(ProtobufMessage::ValidationContext&, messageValidationContext, (), (override));
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, (), (override));
  MOCK_METHOD(Api::Api&, api, (), (override));
  MOCK_METHOD(Http::Context&, httpContext, (), (override));
  MOCK_METHOD(Grpc::Context&, grpcContext, (), (override));
  MOCK_METHOD(Router::Context&, routerContext, (), (override));
  MOCK_METHOD(ProcessContextOptRef, processContext, (), (override));
  MOCK_METHOD(envoy::config::bootstrap::v3::Bootstrap&, bootstrap, (), (override));
  MOCK_METHOD(Server::DrainManager&, drainManager, (), (override));
  MOCK_METHOD(Init::Manager&, initManager, (), (override));
  MOCK_METHOD(ServerLifecycleNotifier&, lifecycleNotifier, (), (override));
  MOCK_METHOD(Regex::Engine&, regexEngine, (), (override));
  MOCK_METHOD(StatsConfig&, statsConfig, (), (override));
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, (), (override));
  MOCK_METHOD(OverloadManager&, overloadManager, (), (override));
  MOCK_METHOD(OverloadManager&, nullOverloadManager, (), (override));
  MOCK_METHOD(bool, shouldBypassOverloadManager, (), (const));
  MOCK_METHOD(bool, healthCheckFailed, (), (const, override));
  MOCK_METHOD(Secret::SecretManager&, secretManager, (), (override));
  MOCK_METHOD(Ssl::ContextManager&, sslContextManager, (), (override));
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
