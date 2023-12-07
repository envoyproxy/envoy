#pragma once

#include "envoy/server/factory_context.h"

#include "source/common/grpc/context_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/quic/quic_stat_names.h"
#include "source/common/router/context_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/transport_sockets/tls/context_manager_impl.h"

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
#include "test/mocks/server/admin.h"
#include "test/mocks/server/drain_manager.h"
#include "test/mocks/server/hot_restart.h"
#include "test/mocks/server/listener_manager.h"
#include "test/mocks/server/options.h"
#include "test/mocks/server/overload_manager.h"
#include "test/mocks/server/server_lifecycle_notifier.h"
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

  MOCK_METHOD(const std::list<Stats::SinkPtr>&, sinks, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, flushInterval, (), (const));
  MOCK_METHOD(bool, flushOnAdmin, (), (const));
  MOCK_METHOD(const Stats::SinkPredicates*, sinkPredicates, (), (const));
  MOCK_METHOD(bool, enableDeferredCreationStats, (), (const));
};

class MockServerFactoryContext : public virtual ServerFactoryContext {
public:
  MockServerFactoryContext();
  ~MockServerFactoryContext() override;

  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, ());
  MOCK_METHOD(Event::Dispatcher&, mainThreadDispatcher, ());
  MOCK_METHOD(const Server::Options&, options, ());
  MOCK_METHOD(const Network::DrainDecision&, drainDecision, ());
  MOCK_METHOD(const LocalInfo::LocalInfo&, localInfo, (), (const));
  MOCK_METHOD(Envoy::Runtime::Loader&, runtime, ());
  MOCK_METHOD(Stats::Scope&, scope, ());
  MOCK_METHOD(Stats::Scope&, serverScope, ());
  MOCK_METHOD(Singleton::Manager&, singletonManager, ());
  MOCK_METHOD(ThreadLocal::Instance&, threadLocal, ());
  MOCK_METHOD(OptRef<Server::Admin>, admin, ());
  MOCK_METHOD(TimeSource&, timeSource, ());
  Event::TestTimeSystem& timeSystem() { return time_system_; }
  MOCK_METHOD(ProtobufMessage::ValidationContext&, messageValidationContext, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());
  MOCK_METHOD(Api::Api&, api, ());
  Http::Context& httpContext() override { return http_context_; }
  Grpc::Context& grpcContext() override { return grpc_context_; }
  Router::Context& routerContext() override { return router_context_; }
  envoy::config::bootstrap::v3::Bootstrap& bootstrap() override { return bootstrap_; }
  MOCK_METHOD(Server::DrainManager&, drainManager, ());
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(ServerLifecycleNotifier&, lifecycleNotifier, ());
  MOCK_METHOD(StatsConfig&, statsConfig, (), ());
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, (), ());
  MOCK_METHOD(OverloadManager&, overloadManager, ());
  MOCK_METHOD(bool, healthCheckFailed, (), (const));

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

  Singleton::ManagerPtr singleton_manager_;
  testing::NiceMock<MockAdmin> admin_;
  Event::GlobalTimeSystem time_system_;
  testing::NiceMock<Api::MockApi> api_;
  testing::NiceMock<MockOverloadManager> overload_manager_;
  Http::ContextImpl http_context_;
  Grpc::ContextImpl grpc_context_;
  Router::ContextImpl router_context_;
  envoy::config::bootstrap::v3::Bootstrap bootstrap_;
  testing::NiceMock<MockOptions> options_;
};

class MockGenericFactoryContext : public GenericFactoryContext {
public:
  MockGenericFactoryContext();
  ~MockGenericFactoryContext() override;

  MOCK_METHOD(ServerFactoryContext&, serverFactoryContext, (), (const));
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, (), (const));
  MOCK_METHOD(Stats::Scope&, scope, (), (const));
  MOCK_METHOD(Init::Manager&, initManager, (), (const));

  NiceMock<MockServerFactoryContext> server_factory_context_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> store_;
  testing::NiceMock<Init::MockManager> init_manager_;
};

// Stateless mock ServerFactoryContext for cases where it needs to be used concurrently in different
// threads. Global state in the MockServerFactoryContext causes thread safety issues in this case.
class StatelessMockServerFactoryContext : public virtual ServerFactoryContext {
public:
  StatelessMockServerFactoryContext() = default;
  ~StatelessMockServerFactoryContext() override = default;

  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, ());
  MOCK_METHOD(Event::Dispatcher&, mainThreadDispatcher, ());
  MOCK_METHOD(const Server::Options&, options, ());
  MOCK_METHOD(const Network::DrainDecision&, drainDecision, ());
  MOCK_METHOD(const LocalInfo::LocalInfo&, localInfo, (), (const));
  MOCK_METHOD(Envoy::Runtime::Loader&, runtime, ());
  MOCK_METHOD(Stats::Scope&, scope, ());
  MOCK_METHOD(Stats::Scope&, serverScope, ());
  MOCK_METHOD(Singleton::Manager&, singletonManager, ());
  MOCK_METHOD(ThreadLocal::Instance&, threadLocal, ());
  MOCK_METHOD(OptRef<Server::Admin>, admin, ());
  MOCK_METHOD(TimeSource&, timeSource, ());
  MOCK_METHOD(Event::TestTimeSystem&, timeSystem, ());
  MOCK_METHOD(ProtobufMessage::ValidationContext&, messageValidationContext, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());
  MOCK_METHOD(Api::Api&, api, ());
  MOCK_METHOD(Http::Context&, httpContext, ());
  MOCK_METHOD(Grpc::Context&, grpcContext, ());
  MOCK_METHOD(Router::Context&, routerContext, ());
  MOCK_METHOD(envoy::config::bootstrap::v3::Bootstrap&, bootstrap, ());
  MOCK_METHOD(Server::DrainManager&, drainManager, ());
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(ServerLifecycleNotifier&, lifecycleNotifier, ());
  MOCK_METHOD(StatsConfig&, statsConfig, (), ());
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, (), ());
  MOCK_METHOD(OverloadManager&, overloadManager, ());
  MOCK_METHOD(bool, healthCheckFailed, (), (const));
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
