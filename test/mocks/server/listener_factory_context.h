#pragma once

#include "envoy/server/configuration.h"
#include "envoy/server/listener_manager.h"

#include "source/common/tls/context_manager_impl.h"

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

  MOCK_METHOD(ServerFactoryContext&, serverFactoryContext, (), (const));
  MOCK_METHOD(TransportSocketFactoryContext&, getTransportSocketFactoryContext, (), (const));
  MOCK_METHOD(const Network::DrainDecision&, drainDecision, ());
  MOCK_METHOD(Init::Manager&, initManager, ());
  MOCK_METHOD(Stats::Scope&, scope, ());
  MOCK_METHOD(Stats::Scope&, listenerScope, ());
  MOCK_METHOD(envoy::config::core::v3::TrafficDirection, direction, (), (const));
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, (), (const));
  MOCK_METHOD(const Network::ListenerInfo&, listenerInfo, (), (const));

  testing::NiceMock<MockServerFactoryContext> server_factory_context_;
  testing::NiceMock<MockDrainManager> drain_manager_;
  testing::NiceMock<Init::MockManager> init_manager_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> store_;
  Stats::Scope& scope_{*store_.rootScope()};
  Stats::IsolatedStoreImpl listener_scope_;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
