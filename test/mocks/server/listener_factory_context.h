#pragma once

#include "envoy/server/configuration.h"
#include "envoy/server/listener_manager.h"

#include "source/extensions/transport_sockets/tls/context_manager_impl.h"

#include "admin.h"
#include "drain_manager.h"
#include "gmock/gmock.h"
#include "instance.h"
#include "overload_manager.h"
#include "server_lifecycle_notifier.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockListenerFactoryContext : public ListenerFactoryContext {
public:
  MockListenerFactoryContext();
  ~MockListenerFactoryContext() override;

  const Network::ListenerConfig& listenerConfig() const override { return listener_config_; }
  MOCK_METHOD(const Network::ListenerConfig&, listenerConfig_, (), (const));
  MOCK_METHOD(ServerFactoryContext&, getServerFactoryContext, (), (const));
  MOCK_METHOD(TransportSocketFactoryContext&, getTransportSocketFactoryContext, (), (const));
  MOCK_METHOD(AccessLog::AccessLogManager&, accessLogManager, ());
  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, ());
  MOCK_METHOD(Event::Dispatcher&, mainThreadDispatcher, ());
  MOCK_METHOD(const Server::Options&, options, ());
  MOCK_METHOD(const Network::DrainDecision&, drainDecision, ());
  MOCK_METHOD(bool, healthCheckFailed, ());
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(ServerLifecycleNotifier&, lifecycleNotifier, ());
  MOCK_METHOD(Envoy::Random::RandomGenerator&, random, ());
  MOCK_METHOD(Envoy::Runtime::Loader&, runtime, ());
  MOCK_METHOD(Stats::Scope&, scope, ());
  MOCK_METHOD(Stats::Scope&, serverScope, ());
  MOCK_METHOD(Singleton::Manager&, singletonManager, ());
  MOCK_METHOD(OverloadManager&, overloadManager, ());
  MOCK_METHOD(ThreadLocal::Instance&, threadLocal, ());
  MOCK_METHOD(OptRef<Server::Admin>, admin, ());
  MOCK_METHOD(Stats::Scope&, listenerScope, ());
  MOCK_METHOD(bool, isQuicListener, (), (const));
  MOCK_METHOD(const LocalInfo::LocalInfo&, localInfo, (), (const));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, listenerMetadata, (), (const));
  MOCK_METHOD(const Envoy::Config::TypedMetadata&, listenerTypedMetadata, (), (const));
  MOCK_METHOD(envoy::config::core::v3::TrafficDirection, direction, (), (const));
  MOCK_METHOD(TimeSource&, timeSource, ());
  Event::TestTimeSystem& timeSystem() { return time_system_; }
  Grpc::Context& grpcContext() override { return grpc_context_; }
  Http::Context& httpContext() override { return http_context_; }
  Router::Context& routerContext() override { return router_context_; }
  MOCK_METHOD(ProcessContextOptRef, processContext, ());
  MOCK_METHOD(ProtobufMessage::ValidationContext&, messageValidationContext, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());
  MOCK_METHOD(Api::Api&, api, ());

  testing::NiceMock<MockServerFactoryContext> server_factory_context_;
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  testing::NiceMock<Upstream::MockClusterManager> cluster_manager_;
  testing::NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<MockDrainManager> drain_manager_;
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<MockServerLifecycleNotifier> lifecycle_notifier_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Envoy::Random::MockRandomGenerator> random_;
  testing::NiceMock<Envoy::Runtime::MockLoader> runtime_loader_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> scope_;
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  Singleton::ManagerPtr singleton_manager_;
  testing::NiceMock<MockAdmin> admin_;
  Stats::IsolatedStoreImpl listener_scope_;
  Event::GlobalTimeSystem time_system_;
  testing::NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  testing::NiceMock<MockOverloadManager> overload_manager_;
  Grpc::ContextImpl grpc_context_;
  Http::ContextImpl http_context_;
  Router::ContextImpl router_context_;
  testing::NiceMock<Api::MockApi> api_;

  Network::MockListenerConfig listener_config_;
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy
