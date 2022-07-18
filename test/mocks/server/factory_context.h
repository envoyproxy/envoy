#pragma once

#include "envoy/server/configuration.h"

#include "source/common/router/context_impl.h"
#include "source/extensions/transport_sockets/tls/context_manager_impl.h"

#include "admin.h"
#include "drain_manager.h"
#include "gmock/gmock.h"
#include "instance.h"
#include "overload_manager.h"
#include "server_lifecycle_notifier.h"
#include "transport_socket_factory_context.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockListenerFactoryContext : public ListenerFactoryContext {
public:
  MockListenerFactoryContext();
  ~MockListenerFactoryContext() override;

  MOCK_METHOD(ServerFactoryContext&, getServerFactoryContext, (), (const));
  MOCK_METHOD(const Network::DrainDecision&, drainDecision, ());
  MOCK_METHOD(ServerLifecycleNotifier&, lifecycleNotifier, ());
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(Stats::Scope&, serverScope, ());
  MOCK_METHOD(Singleton::Manager&, singletonManager, ());
  MOCK_METHOD(ThreadLocal::Instance&, threadLocal, ());
  MOCK_METHOD(Stats::Scope&, listenerScope, ());
  MOCK_METHOD(bool, isQuicListener, (), (const));
  MOCK_METHOD(const LocalInfo::LocalInfo&, localInfo, (), (const));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, listenerMetadata, (), (const));
  MOCK_METHOD(const Envoy::Config::TypedMetadata&, listenerTypedMetadata, (), (const));
  MOCK_METHOD(envoy::config::core::v3::TrafficDirection, direction, (), (const));
  MOCK_METHOD(TimeSource&, timeSource, ());
  MOCK_METHOD(Stats::Scope&, scope, ());

  MOCK_METHOD(const Network::ListenerConfig&, listenerConfig, (), (const));

  Event::TestTimeSystem& timeSystem() { return time_system_; }
  MOCK_METHOD(ProcessContextOptRef, processContext, ());
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());
  MOCK_METHOD(Api::Api&, api, ());

  testing::NiceMock<MockServerFactoryContext> server_factory_context_;
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  testing::NiceMock<MockDrainManager> drain_manager_;
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<MockServerLifecycleNotifier> lifecycle_notifier_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> scope_;
  testing::NiceMock<ThreadLocal::MockInstance> thread_local_;
  Singleton::ManagerPtr singleton_manager_;
  testing::NiceMock<MockAdmin> admin_;
  Stats::IsolatedStoreImpl listener_scope_;
  Event::GlobalTimeSystem time_system_;
  testing::NiceMock<Api::MockApi> api_;
};

class MockFactoryContext : public FilterFactoryContext {
public:
  MockFactoryContext() {
    ON_CALL(*this, getServerFactoryContext())
        .WillByDefault(testing::ReturnRef(mock_server_context_));
    ON_CALL(*this, getDownstreamFactoryContext())
        .WillByDefault(Return(OptRef<DownstreamFactoryContext>{mock_downstream_context_}));
    ON_CALL(mock_downstream_context_, getServerFactoryContext())
        .WillByDefault(ReturnRef(mock_server_context_));
  }

  operator ServerFactoryContext&() { return this->getServerFactoryContext(); }

  MOCK_METHOD(ServerFactoryContext&, getServerFactoryContext, ());
  MOCK_METHOD(OptRef<DownstreamFactoryContext>, getDownstreamFactoryContext, ());

  NiceMock<MockServerFactoryContext> mock_server_context_;
  NiceMock<MockListenerFactoryContext> mock_downstream_context_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
